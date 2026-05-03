#include "API/DX12/Dx12ShaderImpl.h"

#if defined(PE_WIN32)

#include "dxc/dxcapi.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t MAX_COUNT_PER_BINDING = 500;

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
                return L"vs_6_6";
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

            Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
            const std::string &shaderCode = owner->GetCache().GetShaderCode();
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
            return bindCount == 0 ? MAX_COUNT_PER_BINDING : bindCount;
        }

        bool IsPushConstantBinding(const D3D12_SHADER_INPUT_BIND_DESC &binding)
        {
            return binding.Type == D3D_SIT_CBUFFER && binding.BindPoint == 0 && binding.Space == 0;
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

        std::unordered_map<std::string, std::pair<int, int>> ExtractVkBindings(const std::string &shaderCode)
        {
            std::unordered_map<std::string, std::pair<int, int>> bindings;

            std::stringstream stream(shaderCode);
            std::string line;
            const std::regex bindingRegex(R"(\[\[vk::binding\(\s*(\d+)\s*(?:,\s*(\d+))?\s*\)\]\])");
            const std::regex bufferNameRegex(R"(\b[ct]buffer\s+([A-Za-z_][A-Za-z0-9_]*))");
            const std::regex resourceNameRegex(
                R"(\b(?:globallycoherent\s+)?(?:RW)?(?:Texture\w*|StructuredBuffer|ByteAddressBuffer|RaytracingAccelerationStructure|SamplerState|SamplerComparisonState|ConstantBuffer)(?:\s*<[^>]*>)?\s+([A-Za-z_][A-Za-z0-9_]*))");

            while (std::getline(stream, line))
            {
                std::smatch bindingMatch;
                if (!std::regex_search(line, bindingMatch, bindingRegex))
                    continue;

                const int bindPoint = std::stoi(bindingMatch[1].str());
                const int set = bindingMatch[2].matched ? std::stoi(bindingMatch[2].str()) : 0;

                const std::string afterBinding = bindingMatch.suffix().str();
                std::smatch nameMatch;
                if (std::regex_search(afterBinding, nameMatch, bufferNameRegex) ||
                    std::regex_search(afterBinding, nameMatch, resourceNameRegex))
                {
                    bindings[nameMatch[1].str()] = {set, bindPoint};
                }
            }

            return bindings;
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

        const std::unordered_map<std::string, std::pair<int, int>> vkBindings =
            ExtractVkBindings(shader->GetCache().GetShaderCode());

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
            int set = static_cast<int>(binding.Space);
            int bindPoint = static_cast<int>(binding.BindPoint);
            const uint32_t count = GetBindingCount(binding.BindCount);

            if (const auto vkBinding = vkBindings.find(name); vkBinding != vkBindings.end())
            {
                set = vkBinding->second.first;
                bindPoint = vkBinding->second.second;
            }

            if (IsPushConstantBinding(binding))
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
            case D3D_SIT_TBUFFER:
            {
                BufferReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                FillConstantBufferSize(reflection.Get(), binding, desc);
                refl.m_uniformBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_TEXTURE:
            {
                ImageReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                refl.m_images.push_back(desc);
                break;
            }
            case D3D_SIT_SAMPLER:
            {
                SamplerReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                refl.m_samplers.push_back(desc);
                break;
            }
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
            {
                BufferReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                refl.m_storageBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_UAV_RWTYPED:
            {
                ImageReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                refl.m_storageImages.push_back(desc);
                break;
            }
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            {
                BufferReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
                refl.m_storageBuffers.push_back(desc);
                break;
            }
            case D3D_SIT_RTACCELERATIONSTRUCTURE:
            {
                AccelerationStructureReflection desc{};
                desc.name = name;
                desc.set = set;
                desc.binding = bindPoint;
                desc.count = count;
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
