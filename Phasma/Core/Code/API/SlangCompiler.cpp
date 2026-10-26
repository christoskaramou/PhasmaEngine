#include "API/SlangCompiler.h"

#if defined(PE_RUNTIME_SHADER_COMPILER)

#include "Base/Path.h"
#include "Base/Process.h"
#include "rapidjson/document.h"
#if defined(PE_SLANG_VALIDATION)
#include "spirv_cross.hpp"
#endif
#if defined(PE_WIN32)
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include "dxcapi.h"
#else
#include <unistd.h>
#endif

namespace pe
{
    namespace
    {
        const std::filesystem::path &SlangCompilerPath()
        {
            static const std::filesystem::path path = []
            {
                std::filesystem::path compiler = PathFromUtf8(Path::ExecutablePath()) /
#if defined(PE_WIN32)
                                                 "slangc.exe";
#else
                                                 "slangc";
#endif

                std::error_code ec;
                if (!std::filesystem::exists(compiler, ec))
                    compiler = PathFromUtf8(PE_SLANGC_PATH);

                PE_INFO("[Shader] slangc: %s", PathUtf8(compiler).c_str());
                return compiler;
            }();
            return path;
        }

        const char *SlangStageName(PeShaderStageFlags stage)
        {
            switch (stage)
            {
            case PE_SHADER_STAGE_VERTEX:
                return "vertex";
            case PE_SHADER_STAGE_FRAGMENT:
                return "fragment";
            case PE_SHADER_STAGE_COMPUTE:
                return "compute";
            case PE_SHADER_STAGE_RAYGEN_KHR:
                return "raygeneration";
            case PE_SHADER_STAGE_ANY_HIT_KHR:
                return "anyhit";
            case PE_SHADER_STAGE_CLOSEST_HIT_KHR:
                return "closesthit";
            case PE_SHADER_STAGE_MISS_KHR:
                return "miss";
            case PE_SHADER_STAGE_INTERSECTION_KHR:
                return "intersection";
            case PE_SHADER_STAGE_CALLABLE_KHR:
                return "callable";
            default:
                return nullptr;
            }
        }

#if defined(PE_WIN32)
        // Point slangc at the dxcompiler.dll this process already loaded, so -target dxil needs no PATH setup.
        std::string LoadedDxcDirectory()
        {
            HMODULE module = GetModuleHandleW(L"dxcompiler.dll");
            wchar_t path[MAX_PATH]{};
            if (!module || !GetModuleFileNameW(module, path, MAX_PATH))
                return {};
            return PathUtf8(std::filesystem::path(path).parent_path());
        }
#endif

        struct VaryingInput
        {
            std::string semantic;
            uint32_t components = 0; // 1-4 for a 32-bit scalar or vector, 0 for any other type
            uint32_t scalar = 0;     // 0 float, 1 uint, 2 int
        };

        void ReadVaryingType(const rapidjson::Value &type, VaryingInput &input)
        {
            if (!type.IsObject() || !type.HasMember("kind") || !type["kind"].IsString())
                return;
            const std::string_view kind = type["kind"].GetString();
            const rapidjson::Value *scalar = &type;
            uint32_t components = 1;
            if (kind == "vector" && type.HasMember("elementCount") && type["elementCount"].IsUint() &&
                type.HasMember("elementType") && type["elementType"].IsObject())
            {
                components = type["elementCount"].GetUint();
                scalar = &type["elementType"];
            }
            else if (kind != "scalar")
                return;
            if (!scalar->HasMember("scalarType") || !(*scalar)["scalarType"].IsString() || components < 1 || components > 4)
                return;
            const std::string_view name = (*scalar)["scalarType"].GetString();
            input.scalar = name == "float32" ? 0u : name == "uint32" ? 1u
                                                : name == "int32"    ? 2u
                                                                     : 3u;
            input.components = input.scalar < 3 ? components : 0;
        }

        // Slang reflection JSON: the location, HLSL semantic and type of every varying input (struct fields included).
        void CollectInputSemantics(const rapidjson::Value &param, uint32_t base, std::map<uint32_t, VaryingInput> &out)
        {
            // System values (SV_VertexID) carry no binding: they have no location to record.
            if (!param.IsObject() || !param.HasMember("binding") || !param["binding"].IsObject())
                return;
            const rapidjson::Value &binding = param["binding"];
            if (!binding.HasMember("kind") || !binding["kind"].IsString() ||
                std::string_view(binding["kind"].GetString()) != "varyingInput")
                return;
            uint32_t location = base;
            if (binding.HasMember("index") && binding["index"].IsUint())
                location = base + binding["index"].GetUint();
            if (param.HasMember("semanticName") && param["semanticName"].IsString())
            {
                VaryingInput &input = out[location];
                input.semantic = param["semanticName"].GetString();
                if (param.HasMember("semanticIndex") && param["semanticIndex"].IsUint())
                    input.semantic += std::to_string(param["semanticIndex"].GetUint());
                if (param.HasMember("type"))
                    ReadVaryingType(param["type"], input);
            }
            if (param.HasMember("type") && param["type"].IsObject() && param["type"].HasMember("fields") &&
                param["type"]["fields"].IsArray())
            {
                for (const rapidjson::Value &field : param["type"]["fields"].GetArray())
                    CollectInputSemantics(field, location, out);
            }
        }

        std::vector<uint32_t> OpName(uint32_t target, const std::string &name)
        {
            std::vector<uint32_t> literal((name.size() + 4) / 4, 0u); // null-terminated, padded to words
            memcpy(literal.data(), name.data(), name.size());
            std::vector<uint32_t> op{static_cast<uint32_t>(2 + literal.size()) << 16 | 5u, target};
            op.insert(op.end(), literal.begin(), literal.end());
            return op;
        }

        // Slang leaves an unused vertex input out of the module (only -O0 keeps it); DXC keeps it
        // (-fspv-preserve-interface). Reflection::GetVertexAttributes lays a vertex buffer out from the shader's
        // inputs, so a dropped one shifts every later attribute: ShadowsVS ignores its uv, and shadow casters read
        // joints, weights and draw IDs from the wrong bytes. Declare each dropped input again at its location.
        void RestoreDroppedInputs(std::vector<uint8_t> &bytes, const std::map<uint32_t, VaryingInput> &declared,
                                  const std::string &label)
        {
            if (declared.empty() || bytes.size() < 20 || bytes.size() % 4 != 0)
                return;
            std::vector<uint32_t> words(bytes.size() / 4);
            memcpy(words.data(), bytes.data(), bytes.size());

            constexpr uint32_t opName = 5, opMemberName = 6, opEntryPoint = 15, opTypeInt = 21, opTypeFloat = 22,
                               opTypeVector = 23, opTypePointer = 32, opFunction = 54, opVariable = 59, opDecorate = 71,
                               decorationLocation = 30, storageInput = 1;
            std::map<uint32_t, uint32_t> locationById;
            std::set<uint32_t> inputVariables;
            std::map<std::array<uint32_t, 3>, uint32_t> types; // {opcode, operand, operand} -> id
            size_t entryPoint = 0, namesEnd = 0, annotations = 0, functions = 0;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16, opcode = words[i] & 0xffff;
                if (count == 0 || i + count > words.size())
                    return; // malformed: leave the module untouched
                if (opcode == opEntryPoint && !entryPoint)
                    entryPoint = i;
                else if (opcode == opName || opcode == opMemberName)
                    namesEnd = i + count;
                else if (opcode == opDecorate && count >= 4)
                {
                    annotations = annotations ? annotations : i;
                    if (words[i + 2] == decorationLocation)
                        locationById[words[i + 1]] = words[i + 3];
                }
                else if (opcode == opTypeFloat && count == 3)
                    types[{opTypeFloat, words[i + 2], 0}] = words[i + 1];
                else if ((opcode == opTypeInt || opcode == opTypeVector || opcode == opTypePointer) && count == 4)
                    types[{opcode, words[i + 2], words[i + 3]}] = words[i + 1];
                else if (opcode == opVariable && count >= 4 && words[i + 3] == storageInput)
                    inputVariables.insert(words[i + 2]);
                else if (opcode == opFunction && !functions)
                    functions = i;
                i += count;
            }
            if (!entryPoint || !annotations || !functions)
                return;
            std::set<uint32_t> present;
            for (uint32_t id : inputVariables)
                if (const auto location = locationById.find(id); location != locationById.end())
                    present.insert(location->second);

            uint32_t bound = words[3];
            std::vector<uint32_t> names, decorations, globals, entryInputs;
            auto type = [&](std::array<uint32_t, 3> key, std::initializer_list<uint32_t> operands)
            {
                if (const auto it = types.find(key); it != types.end())
                    return it->second;
                const uint32_t id = bound++;
                globals.push_back(static_cast<uint32_t>(2 + operands.size()) << 16 | key[0]);
                globals.push_back(id);
                globals.insert(globals.end(), operands);
                return types[key] = id;
            };
            for (const auto &[location, input] : declared)
            {
                if (present.contains(location))
                    continue;
                if (input.components == 0)
                {
                    PE_WARN("[Slang] %s: unused vertex input %s (location %u) is not a 32-bit scalar or vector and is "
                            "not restored; its vertex layout differs from DXC's",
                            label.c_str(), input.semantic.c_str(), location);
                    continue;
                }
                const uint32_t scalar = input.scalar == 0 ? type({opTypeFloat, 32, 0}, {32})
                                                          : type({opTypeInt, 32, input.scalar - 1}, {32, input.scalar - 1});
                const uint32_t value = input.components == 1 ? scalar
                                                             : type({opTypeVector, scalar, input.components}, {scalar, input.components});
                const uint32_t pointer = type({opTypePointer, storageInput, value}, {storageInput, value});
                const uint32_t variable = bound++;
                globals.insert(globals.end(), {4u << 16 | opVariable, pointer, variable, storageInput});
                decorations.insert(decorations.end(), {4u << 16 | opDecorate, variable, decorationLocation, location});
                const std::vector<uint32_t> name = OpName(variable, "in.var." + input.semantic);
                names.insert(names.end(), name.begin(), name.end());
                entryInputs.push_back(variable);
            }
            if (entryInputs.empty())
                return;

            // Back to front, so the earlier offsets stay valid.
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(functions), globals.begin(), globals.end());
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(annotations), decorations.begin(), decorations.end());
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(namesEnd ? namesEnd : annotations), names.begin(), names.end());
            const uint32_t entryCount = words[entryPoint] >> 16;
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(entryPoint + entryCount), entryInputs.begin(), entryInputs.end());
            words[entryPoint] = static_cast<uint32_t>(entryCount + entryInputs.size()) << 16 | opEntryPoint;
            words[3] = bound;
            bytes.resize(words.size() * 4);
            memcpy(bytes.data(), words.data(), bytes.size());
        }

        // Slang lowers SV_VertexID and SV_InstanceID the D3D way (VertexIndex - BaseVertex, InstanceIndex -
        // BaseInstance); DXC reads Vulkan's raw index, and the voxel shaders take their draw ID from SV_InstanceID's
        // firstInstance. Each such subtraction becomes `index | index`, which keeps the instruction's size and ids.
        void UseRawDrawIndices(std::vector<uint8_t> &bytes)
        {
            if (bytes.size() < 20 || bytes.size() % 4 != 0)
                return;
            std::vector<uint32_t> words(bytes.size() / 4);
            memcpy(words.data(), bytes.data(), bytes.size());

            constexpr uint32_t opLoad = 61, opDecorate = 71, opISub = 130, opBitwiseOr = 197, decorationBuiltIn = 11;
            constexpr uint32_t vertexIndex = 42, instanceIndex = 43, baseVertex = 4424, baseInstance = 4425;
            std::map<uint32_t, uint32_t> builtinByVariable, builtinByLoad;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16, opcode = words[i] & 0xffff;
                if (count == 0 || i + count > words.size())
                    return; // malformed: leave the module untouched
                if (opcode == opDecorate && count == 4 && words[i + 2] == decorationBuiltIn)
                    builtinByVariable[words[i + 1]] = words[i + 3];
                else if (opcode == opLoad && count >= 4 && builtinByVariable.contains(words[i + 3]))
                    builtinByLoad[words[i + 2]] = builtinByVariable[words[i + 3]];
                i += count;
            }
            bool patched = false;
            for (size_t i = 5; i < words.size(); i += words[i] >> 16)
            {
                if (words[i] != (5u << 16 | opISub))
                    continue;
                const auto index = builtinByLoad.find(words[i + 3]), base = builtinByLoad.find(words[i + 4]);
                if (index == builtinByLoad.end() || base == builtinByLoad.end() ||
                    !((index->second == vertexIndex && base->second == baseVertex) ||
                      (index->second == instanceIndex && base->second == baseInstance)))
                    continue;
                words[i] = 5u << 16 | opBitwiseOr;
                words[i + 4] = words[i + 3];
                patched = true;
            }
            if (patched)
                memcpy(bytes.data(), words.data(), bytes.size());
        }

        // Slang names stage inputs after their struct fields ("input.id"); DXC names them "in.var.<SEMANTIC>", which the
        // engine's reflection matches (PE_DRAW_ID). Renaming in the SPIR-V itself keeps cached modules alike too.
        void NameInputsBySemantic(std::vector<uint8_t> &bytes, const std::map<uint32_t, VaryingInput> &semanticByLocation)
        {
            if (semanticByLocation.empty() || bytes.size() < 20 || bytes.size() % 4 != 0)
                return;
            std::vector<uint32_t> words(bytes.size() / 4);
            memcpy(words.data(), bytes.data(), bytes.size());

            constexpr uint32_t opName = 5, opVariable = 59, opDecorate = 71, decorationLocation = 30, storageInput = 1;
            std::map<uint32_t, uint32_t> locationById;
            std::set<uint32_t> inputVariables;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16, opcode = words[i] & 0xffff;
                if (count == 0 || i + count > words.size())
                    return; // malformed: leave the module untouched
                if (opcode == opDecorate && count >= 4 && words[i + 2] == decorationLocation)
                    locationById[words[i + 1]] = words[i + 3];
                else if (opcode == opVariable && count >= 4 && words[i + 3] == storageInput)
                    inputVariables.insert(words[i + 2]);
                i += count;
            }

            std::vector<uint32_t> patched(words.begin(), words.begin() + 5);
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16, opcode = words[i] & 0xffff;
                const uint32_t target = count >= 2 ? words[i + 1] : 0;
                const auto location = locationById.find(target);
                const auto semantic = location == locationById.end() ? semanticByLocation.end()
                                                                     : semanticByLocation.find(location->second);
                if (opcode == opName && inputVariables.contains(target) && semantic != semanticByLocation.end())
                {
                    const std::vector<uint32_t> name = OpName(target, "in.var." + semantic->second.semantic);
                    patched.insert(patched.end(), name.begin(), name.end());
                }
                else
                {
                    patched.insert(patched.end(), words.begin() + i, words.begin() + i + count);
                }
                i += count;
            }
            bytes.resize(patched.size() * 4);
            memcpy(bytes.data(), patched.data(), bytes.size());
        }

        // Slang drops HLSL `precise` from SPIR-V (its DXIL keeps it). DepthVS and GBufferVS need it: GBufferVS
        // depth-tests EQUAL against the prepass, and a skinned position the driver fused differently in each failed.
        // ponytail: marks every float operation in a module whose source says precise, not only the precise
        // values' inputs as DXC does (fewer FMAs in those two vertex shaders); slice per variable if more adopt it.
        void KeepPreciseMaths(std::vector<uint8_t> &bytes)
        {
            if (bytes.size() < 20 || bytes.size() % 4 != 0)
                return;
            std::vector<uint32_t> words(bytes.size() / 4);
            memcpy(words.data(), bytes.data(), bytes.size());

            constexpr uint32_t opDecorate = 71, opMemberDecorate = 72, decorationNoContraction = 42;
            std::vector<uint32_t> decorations;
            size_t annotations = 0;
            for (size_t i = 5; i < words.size();)
            {
                const uint32_t count = words[i] >> 16, opcode = words[i] & 0xffff;
                if (count == 0 || i + count > words.size())
                    return; // malformed: leave the module untouched
                if (!annotations && (opcode == opDecorate || opcode == opMemberDecorate))
                    annotations = i;
                // OpFNegate, OpFAdd, OpFSub, OpFMul, OpFDiv, then OpFRem..OpDot (140-148)
                const bool floatMaths = opcode == 127 || opcode == 129 || opcode == 131 || opcode == 133 ||
                                        opcode == 136 || (opcode >= 140 && opcode <= 148);
                if (floatMaths && count >= 3)
                    decorations.insert(decorations.end(), {3u << 16 | opDecorate, words[i + 2], decorationNoContraction});
                i += count;
            }
            if (!annotations || decorations.empty())
                return;
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(annotations), decorations.begin(), decorations.end());
            bytes.resize(words.size() * 4);
            memcpy(bytes.data(), words.data(), bytes.size());
        }

        // The `precise` keyword, not ParticleVS's preciseFrameIndex or a "// precise:" comment.
        bool UsesPrecise(const std::string &source)
        {
            for (size_t i = source.find("precise"); i != std::string::npos; i = source.find("precise", i + 1))
            {
                const bool wordStart = i == 0 || !(std::isalnum(static_cast<unsigned char>(source[i - 1])) || source[i - 1] == '_');
                if (wordStart && i + 7 < source.size() && std::isspace(static_cast<unsigned char>(source[i + 7])))
                    return true;
            }
            return false;
        }

        std::vector<uint8_t> ReadBytes(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
    } // namespace

    // ponytail: one slangc process per shader (~0.5-1 s cold, cached after); the SDK's in-process Slang API
    // (slang.dll + slang.h) is the upgrade if cold starts with --shader-compiler slang matter.
    SlangCompileStatus CompileWithSlang(Shader *owner,
                                        PeShaderStageFlags stage,
                                        const std::string &source,
                                        const std::vector<Define> &globalDefines,
                                        const std::vector<Define> &localDefines,
                                        const char *profile,
                                        std::vector<uint8_t> &out,
                                        std::string &log)
    {
        const char *stageName = SlangStageName(stage);
        if (!stageName)
        {
            log = "unsupported shader stage";
            return SlangCompileStatus::Failed;
        }
        const bool dxil = std::string_view(profile).starts_with("sm_");
        const std::string &sourcePath = owner->GetCache().GetSourcePath();
        const std::string &entry = owner->GetEntryName();

        // Under the engine's own shader cache, not a shared temp directory another user could plant links in.
        const std::filesystem::path tempDir = PathFromUtf8(Path::Root + "ShaderCache/_slang");
        std::filesystem::create_directories(tempDir);
        // A shader can compile concurrently in different threads and engine processes.
        static std::atomic<uint64_t> nextInvocation{0};
#if defined(PE_WIN32)
        const auto processId = GetCurrentProcessId();
#else
        const auto processId = getpid();
#endif
        const std::string baseName = PathUtf8(PathFromUtf8(sourcePath).stem()) + "_" + entry + "_" +
                                     std::to_string(owner->GetCache().GetHash()) + (dxil ? "_dx12_" : "_vk_") +
                                     std::to_string(processId) + "_" + std::to_string(nextInvocation.fetch_add(1, std::memory_order_relaxed));
        // slangc picks the language by extension: Slang's own syntax (interfaces, generics, autodiff) only parses
        // from a .slang file, so such a source keeps it (and compiles only with --shader-compiler slang).
        const char *sourceExtension = PathFromUtf8(sourcePath).extension() == ".slang" ? ".slang" : ".hlsl";
        struct TempFiles
        {
            std::filesystem::path src, out, json;
            ~TempFiles()
            {
                std::error_code ec;
                for (const std::filesystem::path *path : {&src, &out, &json})
                    std::filesystem::remove(*path, ec);
            }
        } files{tempDir / (baseName + sourceExtension), tempDir / (baseName + (dxil ? ".dxil" : ".spv")), tempDir / (baseName + ".json")};
        {
            std::ofstream file(files.src, std::ios::binary | std::ios::trunc);
            file.write(source.data(), static_cast<std::streamsize>(source.size()));
        }

        // -O3 as DXC: comparing or binding used resources depends on dead-code removal. -fvk-use-dx-layout: DXC's
        // SPIR-V cbuffer/push packing; Slang's default std140/std430 differs. -no-mangle: DXIL keeps the source's
        // resource names, which DX12 reflection maps back to [[vk::binding]]. 31000/39001: TexSamplerDecl's
        // [[vk::combinedImageSampler]] (Slang ignores it) and the texture + sampler sharing one binding.
        std::vector<std::string> args{PathUtf8(files.src),
                                      "-target", dxil ? "dxil" : "spirv",
                                      "-profile", profile,
                                      "-entry", entry,
                                      "-stage", stageName,
                                      "-O3",
                                      "-matrix-layout-row-major",
                                      "-warnings-disable", "31000,39001",
                                      "-I", PathUtf8(PathFromUtf8(sourcePath).parent_path()),
                                      "-o", PathUtf8(files.out)};
        if (dxil)
        {
            args.push_back("-no-mangle");
#if defined(PE_WIN32)
            if (const std::string dxcDir = LoadedDxcDirectory(); !dxcDir.empty())
            {
                args.push_back("-dxc-path");
                args.push_back(dxcDir);
            }
#endif
        }
        else
        {
            args.push_back("-fvk-use-entrypoint-name");
            args.push_back("-fvk-use-dx-layout");
#if defined(PE_DEBUG)
            // DXC's -Zi keeps source lines in SPIR-V (its DXIL debug part goes to a PDB the engine drops). Slang's -g
            // emits NonSemantic.Shader.DebugInfo.100, which needs VK_KHR_shader_non_semantic_info: core from 1.3.
            // Debug builds only: with -O3 it made a MaterialX forward shader take 40 s instead of 3 s (2026-10-26).
            if (std::string_view(profile) == "spirv_1_6")
                args.push_back("-g");
#endif
            args.push_back("-reflection-json");
            args.push_back(PathUtf8(files.json));
        }
        for (const std::vector<Define> *defines : {&globalDefines, &localDefines})
        {
            for (const Define &def : *defines)
            {
                if (def.name.empty())
                    continue;
                args.push_back("-D");
                args.push_back(def.value.empty() ? def.name : def.name + "=" + def.value);
            }
        }

        ProcessOptions options{};
        options.captureOutput = true;
        options.timeoutMs = 120000;
        const std::filesystem::path &compiler = SlangCompilerPath();
        const ProcessResult result = RunProcess(compiler, args, options);
        if (!result.started)
        {
            log = std::string("could not start slangc at '") + PathUtf8(compiler) + "' (set the PE_SLANGC_PATH CMake cache variable)";
            return SlangCompileStatus::NotStarted;
        }
        std::error_code ec;
        if (result.timedOut || result.exitCode != 0 || !std::filesystem::exists(files.out, ec))
        {
            log = (result.timedOut ? std::string("slangc timed out") : "slangc exit " + std::to_string(result.exitCode)) + ":\n" + result.output;
            return SlangCompileStatus::Failed;
        }

        out = ReadBytes(files.out);
        if (!dxil)
        {
            const std::vector<uint8_t> json = ReadBytes(files.json);
            rapidjson::Document document;
            document.Parse(reinterpret_cast<const char *>(json.data()), json.size());
            std::map<uint32_t, VaryingInput> semantics;
            if (!document.HasParseError() && document.IsObject() && document.HasMember("entryPoints") &&
                document["entryPoints"].IsArray() && !document["entryPoints"].Empty())
            {
                const rapidjson::Value &entryPoint = document["entryPoints"][0];
                if (entryPoint.HasMember("parameters") && entryPoint["parameters"].IsArray())
                {
                    for (const rapidjson::Value &param : entryPoint["parameters"].GetArray())
                        CollectInputSemantics(param, 0, semantics);
                }
            }
            if (stage == PE_SHADER_STAGE_VERTEX)
            {
                RestoreDroppedInputs(out, semantics, sourcePath + ":" + entry);
                UseRawDrawIndices(out);
            }
            NameInputsBySemantic(out, semantics);
            if (UsesPrecise(source))
                KeepPreciseMaths(out);
        }
        return SlangCompileStatus::Ok;
    }

#if defined(PE_SLANG_VALIDATION)
    namespace
    {
        using BindingKeys = std::set<std::string>;

        // The absolute byte offset of every leaf member (with array stride and length, matrix stride). Struct nesting is
        // flattened: Slang wraps some arrays in a one-member struct where DXC does not, with identical bytes.
        void FlattenLayout(const spirv_cross::Compiler &compiler, const spirv_cross::SPIRType &type, size_t base, std::string &out)
        {
            for (uint32_t i = 0; i < type.member_types.size(); ++i)
            {
                const spirv_cross::SPIRType &member = compiler.get_type(type.member_types[i]);
                const size_t offset = base + compiler.type_struct_member_offset(type, i);
                if (member.basetype == spirv_cross::SPIRType::Struct && member.array.empty())
                {
                    FlattenLayout(compiler, member, offset, out);
                    continue;
                }
                out += std::to_string(offset);
                if (!member.array.empty())
                    out += "a" + std::to_string(compiler.type_struct_member_array_stride(type, i)) + "x" + std::to_string(member.array[0]);
                if (member.columns > 1)
                    out += "m" + std::to_string(compiler.type_struct_member_matrix_stride(type, i));
                if (member.basetype == spirv_cross::SPIRType::Struct)
                {
                    out += "{";
                    FlattenLayout(compiler, member, 0, out);
                    out += "}";
                }
                out += " ";
            }
        }

        // A block's byte layout and size, so a packing difference is a mismatch rather than a match.
        std::string BlockLayout(const spirv_cross::Compiler &compiler, const spirv_cross::SPIRType &type)
        {
            std::string out = "{";
            FlattenLayout(compiler, type, 0, out);
            return out + "}" + std::to_string(compiler.get_declared_struct_size(type));
        }

        // Only resources the entry point uses are compared, as DXIL reflection does: DXC's -fspv-preserve-bindings
        // keeps unused ones, and Slang leaves them out of the entry's interface even with -preserve-params.
        // RT libraries hold every entry point, so the requested one is selected first.
        BindingKeys SpirvKeys(const void *data, size_t sizeBytes, const std::string &entry, bool rayTracing)
        {
            spirv_cross::Compiler compiler(static_cast<const uint32_t *>(data), sizeBytes / sizeof(uint32_t));
            if (rayTracing)
            {
                const auto entries = compiler.get_entry_points_and_stages();
                const auto ep = std::find_if(entries.begin(), entries.end(), [&](const spirv_cross::EntryPoint &e)
                                             { return e.name == entry; });
                if (ep == entries.end())
                    throw std::runtime_error("entry point '" + entry + "' not found in the module");
                compiler.set_entry_point(ep->name, ep->execution_model);
            }
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources(compiler.get_active_interface_variables());

            BindingKeys keys;
            auto slot = [&](const spirv_cross::Resource &res)
            {
                return std::pair(compiler.get_decoration(res.id, spv::DecorationDescriptorSet),
                                 compiler.get_decoration(res.id, spv::DecorationBinding));
            };
            auto key = [&](const spirv_cross::Resource &res, const std::string &kind)
            {
                const spirv_cross::SPIRType &type = compiler.get_type(res.type_id);
                // 0: runtime- or spec-constant-sized, which the engine binds bindless either way.
                const uint32_t count = type.array.empty() ? 1 : (type.array_size_literal[0] ? type.array[0] : 0);
                const auto [set, binding] = slot(res);
                return std::to_string(set) + "." + std::to_string(binding) + " " + kind + "[" + std::to_string(count) + "]";
            };
            auto imageInfo = [&](const spirv_cross::Resource &res)
            {
                return " dim" + std::to_string(compiler.get_type(res.type_id).image.dim);
            };

            // TexSamplerDecl's [[vk::combinedImageSampler]] makes DXC emit one combined variable; Slang ignores it and
            // emits a separate image and sampler on the binding. The engine builds one combined descriptor from either
            // form, so a texture slot is one key whatever its form.
            std::set<std::pair<uint32_t, uint32_t>> textureSlots;
            for (const auto *list : {&resources.sampled_images, &resources.separate_images})
            {
                for (const spirv_cross::Resource &res : *list)
                {
                    textureSlots.insert(slot(res));
                    keys.insert(key(res, "texture") + imageInfo(res));
                }
            }
            for (const spirv_cross::Resource &res : resources.separate_samplers)
            {
                if (!textureSlots.contains(slot(res)))
                    keys.insert(key(res, "sampler"));
            }
            for (const spirv_cross::Resource &res : resources.storage_images)
                keys.insert(key(res, "storage_image") + imageInfo(res) + " format" +
                            std::to_string(compiler.get_type(res.type_id).image.format));
            for (const spirv_cross::Resource &res : resources.subpass_inputs)
                keys.insert(key(res, "subpass") + " index" +
                            std::to_string(compiler.get_decoration(res.id, spv::DecorationInputAttachmentIndex)));
            for (const spirv_cross::Resource &res : resources.uniform_buffers)
                keys.insert(key(res, "ubo") + " " + BlockLayout(compiler, compiler.get_type(res.base_type_id)));
            for (const spirv_cross::Resource &res : resources.storage_buffers)
                keys.insert(key(res, "ssbo") + " " + BlockLayout(compiler, compiler.get_type(res.base_type_id)));
            for (const spirv_cross::Resource &res : resources.acceleration_structures)
                keys.insert(key(res, "accel"));
            for (const spirv_cross::Resource &res : resources.push_constant_buffers)
                keys.insert("push " + BlockLayout(compiler, compiler.get_type(res.base_type_id)));
            return keys;
        }

#if defined(PE_WIN32)
        // DXIL counterpart of FlattenLayout: Slang's HLSL output wraps a cbuffer's contents in one struct variable
        // where DXC lists each variable, with identical bytes.
        void FlattenDxilLayout(ID3D12ShaderReflectionType *type, UINT base, std::string &out)
        {
            D3D12_SHADER_TYPE_DESC desc{};
            if (!type || FAILED(type->GetDesc(&desc)))
                throw std::runtime_error("DXIL type reflection failed");
            if (desc.Class == D3D_SVC_STRUCT && desc.Elements == 0)
            {
                for (UINT m = 0; m < desc.Members; ++m)
                {
                    ID3D12ShaderReflectionType *member = type->GetMemberTypeByIndex(m);
                    D3D12_SHADER_TYPE_DESC memberDesc{};
                    if (!member || FAILED(member->GetDesc(&memberDesc)))
                        throw std::runtime_error("DXIL member reflection failed");
                    FlattenDxilLayout(member, base + memberDesc.Offset, out);
                }
                return;
            }
            out += std::to_string(base);
            if (desc.Elements)
                out += "x" + std::to_string(desc.Elements);
            if (desc.Class == D3D_SVC_STRUCT)
            {
                out += "{";
                for (UINT m = 0; m < desc.Members; ++m)
                {
                    ID3D12ShaderReflectionType *member = type->GetMemberTypeByIndex(m);
                    D3D12_SHADER_TYPE_DESC memberDesc{};
                    if (!member || FAILED(member->GetDesc(&memberDesc)))
                        throw std::runtime_error("DXIL member reflection failed");
                    FlattenDxilLayout(member, memberDesc.Offset, out);
                }
                out += "}";
            }
            out += " ";
        }

        BindingKeys DxilKeys(const void *data, size_t sizeBytes, bool library)
        {
            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
                throw std::runtime_error("DxcCreateInstance(IDxcUtils) failed");

            BindingKeys keys;
            const DxcBuffer buffer{data, sizeBytes, 0};
            auto add = [&](auto *reflection, const D3D12_SHADER_INPUT_BIND_DESC &desc)
            {
                std::string key = "space" + std::to_string(desc.Space) + "." + std::to_string(desc.BindPoint) + " type" +
                                  std::to_string(desc.Type) + "[" + std::to_string(desc.BindCount) + "]";
                if (desc.Type == D3D_SIT_CBUFFER)
                {
                    D3D12_SHADER_BUFFER_DESC bufferDesc{};
                    auto *constantBuffer = reflection->GetConstantBufferByName(desc.Name);
                    if (constantBuffer && SUCCEEDED(constantBuffer->GetDesc(&bufferDesc)))
                    {
                        key += " bytes" + std::to_string(bufferDesc.Size) + " {";
                        for (UINT v = 0; v < bufferDesc.Variables; ++v)
                        {
                            ID3D12ShaderReflectionVariable *variable = constantBuffer->GetVariableByIndex(v);
                            D3D12_SHADER_VARIABLE_DESC variableDesc{};
                            if (!variable || FAILED(variable->GetDesc(&variableDesc)))
                                throw std::runtime_error("DXIL cbuffer variable reflection failed");
                            FlattenDxilLayout(variable->GetType(), variableDesc.StartOffset, key);
                        }
                        key += "}";
                    }
                }
                else if (desc.Type == D3D_SIT_STRUCTURED || desc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                         desc.Type == D3D_SIT_UAV_APPEND_STRUCTURED || desc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                         desc.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER)
                {
                    key += " stride" + std::to_string(desc.NumSamples); // NumSamples holds the element stride here
                }
                keys.insert(std::move(key));
            };

            if (library)
            {
                Microsoft::WRL::ComPtr<ID3D12LibraryReflection> reflection;
                D3D12_LIBRARY_DESC libDesc{};
                if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection))) || FAILED(reflection->GetDesc(&libDesc)))
                    throw std::runtime_error("DXIL library reflection failed");
                for (UINT i = 0; i < libDesc.FunctionCount; ++i)
                {
                    ID3D12FunctionReflection *function = reflection->GetFunctionByIndex(static_cast<INT>(i));
                    D3D12_FUNCTION_DESC funcDesc{};
                    if (!function || FAILED(function->GetDesc(&funcDesc)))
                        throw std::runtime_error("DXIL function reflection failed");
                    for (UINT r = 0; r < funcDesc.BoundResources; ++r)
                    {
                        D3D12_SHADER_INPUT_BIND_DESC desc{};
                        if (SUCCEEDED(function->GetResourceBindingDesc(r, &desc)))
                            add(function, desc);
                    }
                }
                return keys;
            }

            Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
            D3D12_SHADER_DESC shaderDesc{};
            if (FAILED(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection))) || FAILED(reflection->GetDesc(&shaderDesc)))
                throw std::runtime_error("DXIL shader reflection failed");
            for (UINT r = 0; r < shaderDesc.BoundResources; ++r)
            {
                D3D12_SHADER_INPUT_BIND_DESC desc{};
                if (SUCCEEDED(reflection->GetResourceBindingDesc(r, &desc)))
                    add(reflection.Get(), desc);
            }
            return keys;
        }
#endif

        std::string JoinKeys(const BindingKeys &keys)
        {
            std::string out;
            for (const std::string &key : keys)
                out += (out.empty() ? "" : ", ") + key;
            return out;
        }

        std::atomic<bool> slangcMissing{false};

        void Validate(Shader *owner,
                      PeShaderStageFlags stage,
                      const std::string &source,
                      const std::vector<Define> &globalDefines,
                      const std::vector<Define> &localDefines,
                      const char *profile,
                      const void *dxcBytecode,
                      size_t dxcSizeBytes)
        {
            const bool dxil = std::string_view(profile).starts_with("sm_");
            const bool rayTracing = (stage & PE_SHADER_STAGE_RAY_TRACING) != 0;
            const std::string label = owner->GetCache().GetSourcePath() + ":" + owner->GetEntryName() + (dxil ? " (dxil)" : " (spirv)");

            std::vector<uint8_t> slangBytes;
            std::string log;
            const SlangCompileStatus status = CompileWithSlang(owner, stage, source, globalDefines, localDefines, profile, slangBytes, log);
            if (status == SlangCompileStatus::NotStarted)
            {
                if (!slangcMissing.exchange(true))
                    PE_WARN("[Slang] %s", log.c_str());
                return;
            }
            if (status != SlangCompileStatus::Ok)
            {
                PE_WARN("[Slang] %s failed to compile (%s)", label.c_str(), log.c_str());
                return;
            }

            BindingKeys dxcKeys;
            BindingKeys slangKeys;
            if (dxil)
            {
#if defined(PE_WIN32)
                dxcKeys = DxilKeys(dxcBytecode, dxcSizeBytes, rayTracing);
                slangKeys = DxilKeys(slangBytes.data(), slangBytes.size(), rayTracing);
#endif
            }
            else
            {
                dxcKeys = SpirvKeys(dxcBytecode, dxcSizeBytes, owner->GetEntryName(), rayTracing);
                slangKeys = SpirvKeys(slangBytes.data(), slangBytes.size(), owner->GetEntryName(), rayTracing);
            }

            BindingKeys dxcOnly;
            BindingKeys slangOnly;
            std::set_difference(dxcKeys.begin(), dxcKeys.end(), slangKeys.begin(), slangKeys.end(), std::inserter(dxcOnly, dxcOnly.end()));
            std::set_difference(slangKeys.begin(), slangKeys.end(), dxcKeys.begin(), dxcKeys.end(), std::inserter(slangOnly, slangOnly.end()));
            if (dxcOnly.empty() && slangOnly.empty())
            {
                PE_INFO("[Slang] %s matches DXC (%zu bindings)", label.c_str(), dxcKeys.size());
                return;
            }

            // Kept for inspection, next to the compile's own (already removed) temporaries.
            const std::filesystem::path kept = PathFromUtf8(Path::Root + "ShaderCache/_slang") /
                                               (PathUtf8(PathFromUtf8(owner->GetCache().GetSourcePath()).stem()) + "_" +
                                                owner->GetEntryName() + (dxil ? "_dx12_mismatch.hlsl" : "_vk_mismatch.hlsl"));
            std::ofstream(kept, std::ios::binary | std::ios::trunc).write(source.data(), static_cast<std::streamsize>(source.size()));
            PE_WARN("[Slang] %s reflection mismatch | dxc-only: %s | slang-only: %s | source: %s",
                    label.c_str(), JoinKeys(dxcOnly).c_str(), JoinKeys(slangOnly).c_str(), PathUtf8(kept).c_str());
        }
    } // namespace

    void ValidateShaderWithSlang(Shader *owner,
                                 PeShaderStageFlags stage,
                                 const std::string &source,
                                 const std::vector<Define> &globalDefines,
                                 const std::vector<Define> &localDefines,
                                 const char *profile,
                                 const void *dxcBytecode,
                                 size_t dxcSizeBytes)
    {
        if (!SlangStageName(stage) || !dxcBytecode || dxcSizeBytes == 0 || slangcMissing)
            return;

        // This check must never fail the DXC compile: SPIRV-Cross, DXIL reflection, the filesystem and argument
        // conversion (non-UTF-8 defines) can all throw.
        try
        {
            Validate(owner, stage, source, globalDefines, localDefines, profile, dxcBytecode, dxcSizeBytes);
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Slang] %s:%s could not be validated: %s", owner->GetCache().GetSourcePath().c_str(),
                    owner->GetEntryName().c_str(), e.what());
        }
    }
#endif
} // namespace pe

#endif
