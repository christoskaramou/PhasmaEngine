#include "API/DX12/Dx12ShaderImpl.h"

#if defined(PE_WIN32)

#include "dxcapi.h"

namespace pe
{
    namespace
    {
        struct SourceBinding
        {
            int set = INT32_MIN;
            int binding = INT32_MIN;
            uint32_t structuredStride = 0;
        };

        struct SourceBindings
        {
            std::unordered_map<std::string, SourceBinding> resources;
            std::unordered_set<std::string> pushConstants;
        };

        struct HlslStructDefinition
        {
            std::string body;
            uint32_t size = 0;
            bool resolving = false;
            bool resolved = false;
        };

        std::string Trim(const std::string &text)
        {
            const size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(begin, end - begin + 1);
        }

        std::string StripComments(const std::string &code)
        {
            std::string out;
            out.reserve(code.size());

            bool inLineComment = false;
            bool inBlockComment = false;
            for (size_t i = 0; i < code.size(); ++i)
            {
                const char c = code[i];
                const char next = (i + 1 < code.size()) ? code[i + 1] : '\0';

                if (inLineComment)
                {
                    if (c == '\n')
                    {
                        inLineComment = false;
                        out += '\n';
                    }
                    else
                    {
                        out += ' ';
                    }
                    continue;
                }

                if (inBlockComment)
                {
                    if (c == '*' && next == '/')
                    {
                        inBlockComment = false;
                        out += "  ";
                        ++i;
                    }
                    else
                    {
                        out += (c == '\n') ? '\n' : ' ';
                    }
                    continue;
                }

                if (c == '/' && next == '/')
                {
                    inLineComment = true;
                    out += "  ";
                    ++i;
                    continue;
                }

                if (c == '/' && next == '*')
                {
                    inBlockComment = true;
                    out += "  ";
                    ++i;
                    continue;
                }

                out += c;
            }

            return out;
        }

        size_t FindMatchingBrace(const std::string &text, size_t openBrace)
        {
            uint32_t depth = 0;
            for (size_t i = openBrace; i < text.size(); ++i)
            {
                if (text[i] == '{')
                    ++depth;
                else if (text[i] == '}')
                {
                    --depth;
                    if (depth == 0)
                        return i;
                }
            }
            return std::string::npos;
        }

        std::unordered_map<std::string, HlslStructDefinition> ParseHlslStructDefinitions(const std::string &shaderCode)
        {
            std::unordered_map<std::string, HlslStructDefinition> structs;
            const std::string code = StripComments(shaderCode);
            const std::regex structRegex(R"(\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");

            auto searchStart = code.cbegin();
            std::smatch match;
            while (std::regex_search(searchStart, code.cend(), match, structRegex))
            {
                const size_t matchOffset = static_cast<size_t>(std::distance(code.cbegin(), searchStart)) + static_cast<size_t>(match.position(0));
                const size_t openBrace = code.find('{', matchOffset);
                if (openBrace == std::string::npos)
                    break;

                const size_t closeBrace = FindMatchingBrace(code, openBrace);
                if (closeBrace == std::string::npos)
                    break;

                HlslStructDefinition definition{};
                definition.body = code.substr(openBrace + 1, closeBrace - openBrace - 1);
                structs[match[1].str()] = std::move(definition);
                searchStart = code.cbegin() + static_cast<std::ptrdiff_t>(closeBrace + 1);
            }

            return structs;
        }

        std::vector<std::string> SplitDeclarators(const std::string &declarators)
        {
            std::vector<std::string> result;
            size_t begin = 0;
            while (begin < declarators.size())
            {
                const size_t comma = declarators.find(',', begin);
                const size_t end = comma == std::string::npos ? declarators.size() : comma;
                result.push_back(Trim(declarators.substr(begin, end - begin)));
                if (comma == std::string::npos)
                    break;
                begin = comma + 1;
            }
            return result;
        }

        uint32_t ResolveHlslTypeSize(const std::string &typeName,
                                     std::unordered_map<std::string, HlslStructDefinition> &structs);

        uint32_t ResolveBuiltinHlslTypeSize(const std::string &typeName)
        {
            static const std::regex typeRegex(R"(^(bool|int|uint|float|half|double)([1-4])?(?:x([1-4]))?$)");
            std::smatch match;
            if (!std::regex_match(typeName, match, typeRegex))
                return 0;

            const std::string scalar = match[1].str();
            uint32_t componentSize = sizeof(uint32_t);
            if (scalar == "half")
                componentSize = sizeof(uint16_t);
            else if (scalar == "double")
                componentSize = sizeof(double);

            uint32_t components = 1;
            if (match[2].matched)
                components *= static_cast<uint32_t>(std::stoul(match[2].str()));
            if (match[3].matched)
                components *= static_cast<uint32_t>(std::stoul(match[3].str()));
            return componentSize * components;
        }

        uint32_t ArrayMultiplier(const std::string &declarator)
        {
            uint32_t multiplier = 1;
            static const std::regex arrayRegex(R"(\[\s*(\d+)\s*\])");
            for (std::sregex_iterator it(declarator.begin(), declarator.end(), arrayRegex), end; it != end; ++it)
                multiplier *= static_cast<uint32_t>(std::stoul((*it)[1].str()));
            return multiplier;
        }

        std::string RemoveLeadingHlslQualifiers(std::string declaration)
        {
            static const std::array<std::string, 5> qualifiers = {
                "row_major",
                "column_major",
                "precise",
                "static",
                "const",
            };

            bool removed = true;
            while (removed)
            {
                removed = false;
                declaration = Trim(declaration);
                for (const std::string &qualifier : qualifiers)
                {
                    if (declaration == qualifier)
                        return {};
                    const std::string prefix = qualifier + " ";
                    if (declaration.rfind(prefix, 0) == 0)
                    {
                        declaration = declaration.substr(prefix.size());
                        removed = true;
                        break;
                    }
                }
            }

            return declaration;
        }

        uint32_t ResolveHlslStructSize(const std::string &body,
                                       std::unordered_map<std::string, HlslStructDefinition> &structs)
        {
            uint32_t size = 0;
            std::stringstream stream(body);
            std::string statement;
            static const std::regex declarationRegex(R"(^([A-Za-z_][A-Za-z0-9_]*)(?:\s+(.+))$)");

            while (std::getline(stream, statement, ';'))
            {
                statement = RemoveLeadingHlslQualifiers(statement);
                if (statement.empty())
                    continue;

                std::smatch match;
                if (!std::regex_match(statement, match, declarationRegex))
                    continue;

                const std::string memberType = match[1].str();
                const uint32_t memberSize = ResolveHlslTypeSize(memberType, structs);
                PE_ERROR_IF(memberSize == 0,
                            "DX12 reflection: unsupported StructuredBuffer member type '%s'",
                            memberType.c_str());

                for (std::string declarator : SplitDeclarators(match[2].str()))
                {
                    const size_t semanticPos = declarator.find(':');
                    if (semanticPos != std::string::npos)
                        declarator = declarator.substr(0, semanticPos);
                    declarator = Trim(declarator);
                    if (!declarator.empty())
                        size += memberSize * ArrayMultiplier(declarator);
                }
            }

            return size;
        }

        uint32_t ResolveHlslTypeSize(const std::string &typeName,
                                     std::unordered_map<std::string, HlslStructDefinition> &structs)
        {
            if (const uint32_t builtinSize = ResolveBuiltinHlslTypeSize(typeName))
                return builtinSize;

            auto it = structs.find(typeName);
            if (it == structs.end())
                return 0;

            HlslStructDefinition &definition = it->second;
            if (definition.resolved)
                return definition.size;

            PE_ERROR_IF(definition.resolving,
                        "DX12 reflection: recursive StructuredBuffer element type '%s' is unsupported",
                        typeName.c_str());

            definition.resolving = true;
            definition.size = ResolveHlslStructSize(definition.body, structs);
            definition.resolving = false;
            definition.resolved = true;
            return definition.size;
        }

        std::wstring ConvertUtf8ToWide(const std::string &str)
        {
            if (str.empty())
                return std::wstring();

            int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
            if (sizeNeeded <= 0)
                return std::wstring();

            std::wstring wideStr(sizeNeeded, 0);
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wideStr.data(), sizeNeeded);
            wideStr.resize(sizeNeeded - 1);
            return wideStr;
        }

        void PrintDxcError(IDxcResult *result)
        {
            Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            if (errors && errors->GetStringLength() > 0)
                PE_INFO(errors->GetStringPointer());
        }

        LPCWSTR GetStageProfile(PeShaderStageFlags stage)
        {
            if (stage == PE_SHADER_STAGE_VERTEX)
                return L"vs_6_8";
            if (stage == PE_SHADER_STAGE_FRAGMENT)
                return L"ps_6_6";
            if (stage == PE_SHADER_STAGE_COMPUTE)
                return L"cs_6_6";
            if (stage & (PE_SHADER_STAGE_RAYGEN_KHR | PE_SHADER_STAGE_ANY_HIT_KHR | PE_SHADER_STAGE_CLOSEST_HIT_KHR |
                         PE_SHADER_STAGE_MISS_KHR | PE_SHADER_STAGE_INTERSECTION_KHR | PE_SHADER_STAGE_CALLABLE_KHR))
                return L"lib_6_6";

            PE_ERROR("[Shader] Invalid DX12 shader stage");
            return L"";
        }

        bool CompileHlslToDxil(Shader *owner,
                               PeShaderStageFlags stage,
                               const std::vector<Define> &globalDefines,
                               const std::vector<Define> &localDefines,
                               std::vector<uint8_t> &outDxil)
        {
            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcUtils");
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
            hr = utils->CreateDefaultIncludeHandler(&includeHandler);
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcIncludeHandler");
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
            hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
            if (FAILED(hr))
            {
                PE_INFO("Failed to create IDxcCompiler3");
                return false;
            }

            auto prepareDx12ShaderCode = [](const std::string &shaderCode)
            {
                auto withMacroRegisters = shaderCode;
                auto replaceAll = [](std::string &text, const std::string &from, const std::string &to)
                {
                    size_t pos = 0;
                    while ((pos = text.find(from, pos)) != std::string::npos)
                    {
                        text.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                };

                replaceAll(withMacroRegisters,
                           "Texture2D tex; \\",
                           "Texture2D tex : register(t##bind, space##set); \\");
                replaceAll(withMacroRegisters,
                           "TextureCube tex; \\",
                           "TextureCube tex : register(t##bind, space##set); \\");
                replaceAll(withMacroRegisters,
                           "SamplerState sampler_##tex;",
                           "SamplerState sampler_##tex : register(s##bind, space##set);");

                std::stringstream in(withMacroRegisters);
                std::string out;
                std::string line;
                const std::regex bindingRegex(R"(\[\[vk::binding\(\s*(\d+)\s*(?:,\s*(\d+))?\s*\)\]\])");

                while (std::getline(in, line))
                {
                    std::smatch match;
                    if (std::regex_search(line, match, bindingRegex) && line.find(": register(") == std::string::npos)
                    {
                        const std::string binding = match[1].str();
                        const std::string set = match[2].matched ? match[2].str() : "0";
                        const std::string suffix = match.suffix().str();
                        std::string regType;

                        if (std::regex_search(suffix, std::regex(R"(\b(?:cbuffer|ConstantBuffer)\b)")))
                            regType = "b";
                        else if (std::regex_search(suffix, std::regex(R"(\btbuffer\b)")))
                            regType = "t";
                        else if (std::regex_search(suffix, std::regex(R"(\bSampler(?:State|ComparisonState)\b)")))
                            regType = "s";
                        else if (std::regex_search(suffix, std::regex(R"(\b(?:globallycoherent\s+)?RW(?:Texture\w*|StructuredBuffer|ByteAddressBuffer|Buffer)\b)")))
                            regType = "u";
                        else if (std::regex_search(suffix, std::regex(R"(\b(?:Texture\w*|StructuredBuffer|ByteAddressBuffer|RaytracingAccelerationStructure|ConstantBuffer)\b)")))
                            regType = "t";

                        if (!regType.empty())
                        {
                            const std::string annotation = " : register(" + regType + binding + ", space" + set + ")";
                            size_t insertPos = line.find('{', match.position() + match.length());
                            if (insertPos == std::string::npos)
                                insertPos = line.find(';', match.position() + match.length());
                            if (insertPos != std::string::npos)
                                line.insert(insertPos, annotation);
                        }
                    }

                    out += line;
                    out += '\n';
                }

                return out;
            };

            Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
            const std::string shaderCode = "#define PE_DX12 1\n" + prepareDx12ShaderCode(owner->GetCache().GetShaderCode());
            hr = utils->CreateBlob(shaderCode.data(), static_cast<uint32_t>(shaderCode.size()), CP_UTF8, &source);
            if (FAILED(hr))
            {
                PE_INFO("Failed to create DXC source blob");
                return false;
            }

            DxcBuffer sourceBuffer{};
            sourceBuffer.Ptr = source->GetBufferPointer();
            sourceBuffer.Size = source->GetBufferSize();
            sourceBuffer.Encoding = 0;

            std::vector<std::wstring> ownedStrings;
            ownedStrings.reserve(2 + globalDefines.size() + localDefines.size());

            std::vector<LPCWSTR> args{};
            args.push_back(L"-T");
            args.push_back(GetStageProfile(stage));
            args.push_back(L"-E");
            ownedStrings.push_back(ConvertUtf8ToWide(owner->GetEntryName()));
            args.push_back(ownedStrings.back().c_str());

            const std::filesystem::path includePath = std::filesystem::path(owner->GetCache().GetSourcePath()).parent_path();
            ownedStrings.push_back(ConvertUtf8ToWide(includePath.string()));
            args.push_back(L"-I");
            args.push_back(ownedStrings.back().c_str());

            args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
            args.push_back(L"-Wno-ignored-attributes");
            args.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

#if PE_DEBUG_MODE
            args.push_back(DXC_ARG_DEBUG);
#else
            args.push_back(L"-Qstrip_debug");
            args.push_back(L"-O3");
#endif

            auto addDefine = [&](const Define &def)
            {
                if (def.name.empty())
                    return;

                ownedStrings.push_back(ConvertUtf8ToWide(def.value.empty() ? def.name : def.name + "=" + def.value));
                args.push_back(L"-D");
                args.push_back(ownedStrings.back().c_str());
            };

            for (const Define &def : globalDefines)
                addDefine(def);
            for (const Define &def : localDefines)
                addDefine(def);

            Microsoft::WRL::ComPtr<IDxcResult> result;
            hr = compiler->Compile(&sourceBuffer,
                                   args.data(),
                                   static_cast<uint32_t>(args.size()),
                                   includeHandler.Get(),
                                   IID_PPV_ARGS(&result));
            if (FAILED(hr) || !result)
            {
                PE_INFO("DXC compile call failed");
                return false;
            }

            HRESULT compileStatus = S_OK;
            result->GetStatus(&compileStatus);
            if (FAILED(compileStatus))
            {
                PrintDxcError(result.Get());
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcBlob> object;
            result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            if (!object)
            {
                PrintDxcError(result.Get());
                return false;
            }

            const auto *bytes = static_cast<const uint8_t *>(object->GetBufferPointer());
            outDxil.assign(bytes, bytes + object->GetBufferSize());
            return true;
        }

        uint32_t GetBindingCount(UINT bindCount)
        {
            return bindCount == 0 ? PE_MAX_DESCRIPTORS_PER_BINDING : bindCount;
        }

        bool IsPushConstantBinding(const D3D12_SHADER_INPUT_BIND_DESC &binding, const SourceBindings &sourceBindings)
        {
            const std::string name = binding.Name ? binding.Name : "";
            if (binding.Type != D3D_SIT_CBUFFER || binding.BindPoint != 0 || binding.Space != 0)
                return false;
            if (sourceBindings.pushConstants.find(name) != sourceBindings.pushConstants.end())
                return true;
            // DXC compiles `[[vk::push_constant]] T pc;` to an unattached cbuffer
            // reflected as "$Globals" at b0/space0; accept it whenever the source
            // declared a push-constant block.
            return name == "$Globals" && !sourceBindings.pushConstants.empty();
        }

        uint32_t CountMaskComponents(BYTE mask)
        {
            uint32_t count = 0;
            for (uint32_t bit = 0; bit < 4; ++bit)
            {
                if (mask & (1 << bit))
                    ++count;
            }
            return count;
        }

        ReflectionVariableType GetReflectionVariableType(D3D_REGISTER_COMPONENT_TYPE componentType)
        {
            switch (componentType)
            {
            case D3D_REGISTER_COMPONENT_UINT32:
                return ReflectionVariableType::UInt;
            case D3D_REGISTER_COMPONENT_SINT32:
                return ReflectionVariableType::SInt;
            case D3D_REGISTER_COMPONENT_FLOAT32:
                return ReflectionVariableType::SFloat;
            default:
                return ReflectionVariableType::None;
            }
        }

        ::PeFormat GetAttributeFormat(uint32_t totalSize, ReflectionVariableType type)
        {
            switch (type)
            {
            case ReflectionVariableType::SInt:
                switch (totalSize)
                {
                case 4:
                    return ::PE_FORMAT_R32_SINT;
                case 8:
                    return ::PE_FORMAT_R32G32_SINT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_SINT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_SINT;
                }
                break;
            case ReflectionVariableType::UInt:
                switch (totalSize)
                {
                case 4:
                    return ::PE_FORMAT_R32_UINT;
                case 8:
                    return ::PE_FORMAT_R32G32_UINT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_UINT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_UINT;
                }
                break;
            case ReflectionVariableType::SFloat:
                switch (totalSize)
                {
                case 4:
                    return ::PE_FORMAT_R32_SFLOAT;
                case 8:
                    return ::PE_FORMAT_R32G32_SFLOAT;
                case 12:
                    return ::PE_FORMAT_R32G32B32_SFLOAT;
                case 16:
                    return ::PE_FORMAT_R32G32B32A32_SFLOAT;
                }
                break;
            default:
                break;
            }

            PE_ERROR("[Shader] Unsupported DX12 attribute type");
            return ::PE_FORMAT_UNDEFINED;
        }

        ShaderInOutDesc FillSignatureParameter(const D3D12_SIGNATURE_PARAMETER_DESC &param)
        {
            const uint32_t componentCount = CountMaskComponents(param.Mask);
            const uint32_t totalSize = componentCount * sizeof(uint32_t);
            const ReflectionVariableType reflectionType = GetReflectionVariableType(param.ComponentType);

            ShaderInOutDesc desc{};
            desc.name = param.SemanticName ? param.SemanticName : "";
            desc.location = static_cast<int>(param.Register);
            desc.semanticIndex = param.SemanticIndex;
            desc.size = totalSize;
            if (totalSize > 0 && reflectionType != ReflectionVariableType::None)
                desc.format = GetAttributeFormat(totalSize, reflectionType);
            desc.binding = 0;
            return desc;
        }

        void FillConstantBufferSize(ID3D12ShaderReflection *reflection,
                                    const D3D12_SHADER_INPUT_BIND_DESC &binding,
                                    BufferReflection &desc)
        {
            ID3D12ShaderReflectionConstantBuffer *constantBuffer = reflection->GetConstantBufferByName(binding.Name);
            if (!constantBuffer)
                return;

            D3D12_SHADER_BUFFER_DESC bufferDesc{};
            if (SUCCEEDED(constantBuffer->GetDesc(&bufferDesc)))
                desc.bufferSize = bufferDesc.Size;
        }

        SourceBindings ExtractSourceBindings(const std::string &shaderCode)
        {
            SourceBindings bindings;
            auto structDefinitions = ParseHlslStructDefinitions(shaderCode);

            std::stringstream stream(shaderCode);
            std::string line;
            bool pendingPushConstant = false;
            const std::regex bindingRegex(R"(\[\[vk::binding\(\s*(\d+)\s*(?:,\s*(\d+))?\s*\)\]\])");
            const std::regex bufferNameRegex(R"(\b[ct]buffer\s+([A-Za-z_][A-Za-z0-9_]*))");
            const std::regex structuredResourceRegex(
                R"(\b(?:globallycoherent\s+)?(?:(?:RW|Append|Consume)StructuredBuffer|StructuredBuffer)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>\s+([A-Za-z_][A-Za-z0-9_]*))");
            const std::regex resourceNameRegex(
                R"(\b(?:globallycoherent\s+)?(?:(?:RW|Append|Consume)?StructuredBuffer|(?:RW)?(?:Texture\w*|ByteAddressBuffer|Buffer)|RaytracingAccelerationStructure|SamplerState|SamplerComparisonState|ConstantBuffer)(?:\s*<[^>]*>)?\s+([A-Za-z_][A-Za-z0-9_]*))");
            const std::regex macroRegex(R"(\b(?:TexSamplerDecl|CubeSamplerDecl)\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))");
            const std::regex pushConstantRegex(R"((?:(?:ConstantBuffer\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>)|([A-Za-z_][A-Za-z0-9_]*))\s+([A-Za-z_][A-Za-z0-9_]*))");

            while (std::getline(stream, line))
            {
                const size_t commentPos = line.find("//");
                if (commentPos != std::string::npos)
                    line = line.substr(0, commentPos);

                const bool hasPushConstantAttribute = line.find("[[vk::push_constant]]") != std::string::npos;
                if (hasPushConstantAttribute)
                {
                    pendingPushConstant = true;
                    line = line.substr(line.find("[[vk::push_constant]]") + std::strlen("[[vk::push_constant]]"));
                }
                if (pendingPushConstant)
                {
                    std::smatch pushMatch;
                    if (std::regex_search(line, pushMatch, pushConstantRegex))
                    {
                        bindings.pushConstants.insert(pushMatch[3].str());
                        pendingPushConstant = false;
                    }
                    if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos && !hasPushConstantAttribute)
                        pendingPushConstant = false;
                }

                std::smatch macroMatch;
                if (std::regex_search(line, macroMatch, macroRegex))
                {
                    SourceBinding binding{};
                    binding.binding = std::stoi(macroMatch[1].str());
                    binding.set = std::stoi(macroMatch[2].str());
                    const std::string texName = macroMatch[3].str();
                    bindings.resources[texName] = binding;
                    bindings.resources["sampler_" + texName] = binding;
                    continue;
                }

                std::smatch bindingMatch;
                if (!std::regex_search(line, bindingMatch, bindingRegex))
                    continue;

                SourceBinding binding{};
                binding.binding = std::stoi(bindingMatch[1].str());
                binding.set = bindingMatch[2].matched ? std::stoi(bindingMatch[2].str()) : 0;

                const std::string afterBinding = bindingMatch.suffix().str();
                std::smatch nameMatch;
                if (std::regex_search(afterBinding, nameMatch, structuredResourceRegex))
                {
                    const std::string elementType = nameMatch[1].str();
                    const std::string resourceName = nameMatch[2].str();
                    binding.structuredStride = ResolveHlslTypeSize(elementType, structDefinitions);
                    PE_ERROR_IF(binding.structuredStride == 0,
                                "DX12 reflection: could not resolve StructuredBuffer '%s' element type '%s'",
                                resourceName.c_str(),
                                elementType.c_str());
                    bindings.resources[resourceName] = binding;
                    continue;
                }

                if (std::regex_search(afterBinding, nameMatch, bufferNameRegex) ||
                    std::regex_search(afterBinding, nameMatch, resourceNameRegex))
                {
                    bindings.resources[nameMatch[1].str()] = binding;
                }
            }

            return bindings;
        }

        SourceBinding RequireSourceBinding(const SourceBindings &bindings, const D3D12_SHADER_INPUT_BIND_DESC &binding)
        {
            const std::string name = binding.Name ? binding.Name : "";
            auto it = bindings.resources.find(name);
            PE_ERROR_IF(it == bindings.resources.end(),
                        "DX12 reflection: resource '%s' is missing a recoverable [[vk::binding]] annotation",
                        name.c_str());
            return it->second;
        }

        template <typename T>
        void FillBindingMetadata(T &desc, const SourceBinding &sourceBinding, const D3D12_SHADER_INPUT_BIND_DESC &dxBinding)
        {
            const std::string name = dxBinding.Name ? dxBinding.Name : "";
            PE_ERROR_IF(static_cast<int>(dxBinding.Space) != sourceBinding.set,
                        "DX12 reflection: resource '%s' DXIL space %u disagrees with source set %d "
                        "(per-space root signature requires Space == set)",
                        name.c_str(),
                        dxBinding.Space,
                        sourceBinding.set);
            PE_ERROR_IF(static_cast<int>(dxBinding.BindPoint) != sourceBinding.binding,
                        "DX12 reflection: resource '%s' DXIL BindPoint %u disagrees with source binding %d "
                        "(table-offset invariant requires BindPoint == binding)",
                        name.c_str(),
                        dxBinding.BindPoint,
                        sourceBinding.binding);
            desc.set = sourceBinding.set;
            desc.binding = sourceBinding.binding;
            desc.dxRegister = static_cast<int>(dxBinding.BindPoint);
            desc.dxSpace = static_cast<int>(dxBinding.Space);
        }
    } // namespace

    Dx12ShaderImpl::Dx12ShaderImpl(Shader *owner, const ShaderDesc &desc)
        : m_owner{owner}
    {
        PE_ERROR_IF(desc.type != ShaderCodeType::HLSL, "DX12 shader backend only supports HLSL input");
        PE_ERROR_IF(!CompileHlslToDxil(owner, desc.stage, Shader::m_globalDefines, desc.defines, m_dxil),
                    std::string("Failed to compile DXIL shader: " + desc.sourcePath).c_str());

        m_owner->m_cache.WriteBytecodeToFile(m_dxil);
    }

    Dx12ShaderImpl::Dx12ShaderImpl(Shader *owner, std::vector<uint8_t> dxil)
        : m_owner{owner}, m_dxil{std::move(dxil)}
    {
    }

    D3D12_SHADER_BYTECODE Dx12ShaderImpl::GetBytecode() const
    {
        D3D12_SHADER_BYTECODE bytecode{};
        bytecode.pShaderBytecode = m_dxil.data();
        bytecode.BytecodeLength = m_dxil.size();
        return bytecode;
    }

    void PopulateReflectionFromDxil(Reflection &refl, Shader *shader)
    {
        refl.m_shader = shader;

        const uint8_t *dxilData = GetDx12ShaderDxil(shader);
        const size_t dxilSize = GetDx12ShaderDxilSizeBytes(shader);
        if (!dxilData || dxilSize == 0)
            return;

        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        if (FAILED(hr))
        {
            PE_ERROR("[Shader] Failed to create IDxcUtils for DXIL reflection");
            return;
        }

        DxcBuffer reflectBuffer{};
        reflectBuffer.Ptr = dxilData;
        reflectBuffer.Size = dxilSize;
        reflectBuffer.Encoding = 0;

        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        hr = utils->CreateReflection(&reflectBuffer, IID_PPV_ARGS(&reflection));
        if (FAILED(hr) || !reflection)
        {
            PE_ERROR("[Shader] Failed to create DXIL reflection");
            return;
        }

        D3D12_SHADER_DESC shaderDesc{};
        if (FAILED(reflection->GetDesc(&shaderDesc)))
            return;

        const std::string &reflectionSource = shader->GetReflectionSource().empty()
                                                  ? shader->GetCache().GetShaderCode()
                                                  : shader->GetReflectionSource();
        const SourceBindings sourceBindings = ExtractSourceBindings(reflectionSource);

        for (uint32_t i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflection->GetInputParameterDesc(i, &param)))
                continue;
            if (param.SystemValueType != D3D_NAME_UNDEFINED)
                continue;

            refl.m_inputs.push_back(FillSignatureParameter(param));
        }
        std::sort(refl.m_inputs.begin(), refl.m_inputs.end(),
                  [](auto &a, auto &b)
                  { return a.location < b.location; });

        for (uint32_t i = 0; i < shaderDesc.OutputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflection->GetOutputParameterDesc(i, &param)))
                continue;
            if (param.SystemValueType != D3D_NAME_UNDEFINED)
                continue;

            ShaderInOutDesc desc = FillSignatureParameter(param);
            desc.binding = INT32_MIN;
            refl.m_outputs.push_back(desc);
        }
        std::sort(refl.m_outputs.begin(), refl.m_outputs.end(),
                  [](auto &a, auto &b)
                  { return a.location < b.location; });

        for (uint32_t i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &binding)))
                continue;

            const std::string name = binding.Name ? binding.Name : "";
            const uint32_t count = GetBindingCount(binding.BindCount);

            if (IsPushConstantBinding(binding, sourceBindings))
            {
                BufferReflection buffer{};
                FillConstantBufferSize(reflection.Get(), binding, buffer);
                refl.m_pushConstants.name = name;
                refl.m_pushConstants.structName = name;
                refl.m_pushConstants.size = buffer.bufferSize;
                continue;
            }

            switch (binding.Type)
            {
            case D3D_SIT_CBUFFER:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                PE_ERROR_IF(binding.Space == 0 && binding.BindPoint == 0,
                            "DX12 reflection: ordinary cbuffer '%s' compiled to b0/space0 reserved for push constants",
                            name.c_str());
                BufferReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                FillConstantBufferSize(reflection.Get(), binding, desc);
                refl.m_uniformBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_TBUFFER:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                BufferReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                desc.kind = PeBufferKind::Structured;
                FillConstantBufferSize(reflection.Get(), binding, desc);
                refl.m_storageBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_TEXTURE:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                ImageReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                refl.m_images.push_back(desc);
                break;
            }
            case D3D_SIT_SAMPLER:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                SamplerReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                refl.m_samplers.push_back(desc);
                break;
            }
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                BufferReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                desc.kind = (binding.Type == D3D_SIT_BYTEADDRESS)
                                ? PeBufferKind::ByteAddress
                                : PeBufferKind::Structured;
                if (desc.kind == PeBufferKind::Structured)
                {
                    desc.structuredStride = sourceBinding.structuredStride;
                    PE_ERROR_IF(desc.structuredStride == 0,
                                "DX12 reflection: StructuredBuffer '%s' has no element stride",
                                name.c_str());
                }
                refl.m_storageBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_UAV_RWTYPED:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                ImageReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                refl.m_storageImages.push_back(desc);
                break;
            }
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                BufferReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                desc.kind = PeBufferKind::StorageRW;
                if (binding.Type != D3D_SIT_UAV_RWBYTEADDRESS)
                {
                    desc.structuredStride = sourceBinding.structuredStride;
                    PE_ERROR_IF(desc.structuredStride == 0,
                                "DX12 reflection: RWStructuredBuffer '%s' has no element stride",
                                name.c_str());
                }
                refl.m_storageBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_RTACCELERATIONSTRUCTURE:
            {
                SourceBinding sourceBinding = RequireSourceBinding(sourceBindings, binding);
                AccelerationStructureReflection desc{};
                desc.name = name;
                desc.count = count;
                FillBindingMetadata(desc, sourceBinding, binding);
                refl.m_accelerationStructures.push_back(desc);
                break;
            }
            default:
                break;
            }
        }
    }
} // namespace pe

#endif // PE_WIN32
