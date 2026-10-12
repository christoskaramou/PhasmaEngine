#include "API/SlangShaderValidation.h"

#if defined(PE_SHADER_COMPILER_SLANG)

#include "Base/Path.h"
#include "Base/Process.h"
#include "spirv_cross.hpp"
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
        using BindingKeys = std::set<std::string>;

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

        std::string JoinKeys(const BindingKeys &keys)
        {
            std::string out;
            for (const std::string &key : keys)
                out += (out.empty() ? "" : ", ") + key;
            return out;
        }

        std::atomic<bool> slangcMissing{false};

        void Validate(Shader *owner,
                      const char *stageName,
                      bool rayTracing,
                      const std::string &source,
                      const std::vector<Define> &globalDefines,
                      const std::vector<Define> &localDefines,
                      bool dxil,
                      const void *dxcBytecode,
                      size_t dxcSizeBytes)
        {
            const std::string &sourcePath = owner->GetCache().GetSourcePath();
            const std::string &entry = owner->GetEntryName();
            const std::string label = sourcePath + ":" + entry + (dxil ? " (dxil)" : " (spirv)");

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
            // Removed on every exit, exceptions included, except a mismatch, which keeps them for inspection.
            struct TempFiles
            {
                std::filesystem::path src;
                std::filesystem::path out;
                bool keep = false;
                ~TempFiles()
                {
                    if (keep)
                        return;
                    std::error_code ec;
                    std::filesystem::remove(src, ec);
                    std::filesystem::remove(out, ec);
                }
            } files{tempDir / (baseName + ".hlsl"), tempDir / (baseName + (dxil ? ".dxil" : ".spv"))};
            {
                std::ofstream file(files.src, std::ios::binary | std::ios::trunc);
                file.write(source.data(), static_cast<std::streamsize>(source.size()));
            }

            // -O3 as DXC: comparing used resources depends on dead-code removal (RayTrace anyhit keeps an unused push
            // block at Slang's default level). -fvk-use-dx-layout: DXC's SPIR-V cbuffer/push packing; Slang's default
            // std140/std430 differs. 39001: Slang's overlap warning for TexSamplerDecl's texture + sampler binding.
            std::vector<std::string> args{PathUtf8(files.src),
                                          "-target", dxil ? "dxil" : "spirv",
                                          "-profile", dxil ? "sm_6_6" : "spirv_1_6",
                                          "-entry", entry,
                                          "-stage", stageName,
                                          "-O3",
                                          "-matrix-layout-row-major",
                                          "-warnings-disable", "39001",
                                          "-I", PathUtf8(PathFromUtf8(sourcePath).parent_path()),
                                          "-o", PathUtf8(files.out)};
            if (!dxil)
            {
                args.push_back("-fvk-use-entrypoint-name");
                args.push_back("-fvk-use-dx-layout");
            }
#if defined(PE_WIN32)
            if (dxil)
            {
                if (const std::string dxcDir = LoadedDxcDirectory(); !dxcDir.empty())
                {
                    args.push_back("-dxc-path");
                    args.push_back(dxcDir);
                }
            }
#endif
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
            const ProcessResult result = RunProcess(PathFromUtf8(PE_SLANGC_PATH), args, options);
            if (!result.started)
            {
                if (!slangcMissing.exchange(true))
                    PE_WARN("[Slang] Could not start slangc at '%s' (set the PE_SLANGC_PATH CMake cache variable)", PE_SLANGC_PATH);
                return;
            }
            std::error_code ec;
            if (result.timedOut || result.exitCode != 0 || !std::filesystem::exists(files.out, ec))
            {
                PE_WARN("[Slang] %s failed to compile (%s):\n%s", label.c_str(),
                        result.timedOut ? "timed out" : ("exit " + std::to_string(result.exitCode)).c_str(), result.output.c_str());
                return;
            }

            std::vector<char> slangBytes;
            {
                std::ifstream file(files.out, std::ios::binary);
                slangBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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
                dxcKeys = SpirvKeys(dxcBytecode, dxcSizeBytes, entry, rayTracing);
                slangKeys = SpirvKeys(slangBytes.data(), slangBytes.size(), entry, rayTracing);
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

            files.keep = true;
            PE_WARN("[Slang] %s reflection mismatch | dxc-only: %s | slang-only: %s | source: %s",
                    label.c_str(), JoinKeys(dxcOnly).c_str(), JoinKeys(slangOnly).c_str(), PathUtf8(files.src).c_str());
        }
    } // namespace

    void ValidateShaderWithSlang(Shader *owner,
                                 PeShaderStageFlags stage,
                                 const std::string &source,
                                 const std::vector<Define> &globalDefines,
                                 const std::vector<Define> &localDefines,
                                 bool dxil,
                                 const void *dxcBytecode,
                                 size_t dxcSizeBytes)
    {
        const char *stageName = SlangStageName(stage);
        if (!stageName || !dxcBytecode || dxcSizeBytes == 0 || slangcMissing)
            return;

        // This check must never fail the DXC compile: SPIRV-Cross, DXIL reflection, the filesystem and argument
        // conversion (non-UTF-8 defines) can all throw.
        try
        {
            Validate(owner, stageName, (stage & PE_SHADER_STAGE_RAY_TRACING) != 0, source, globalDefines, localDefines,
                     dxil, dxcBytecode, dxcSizeBytes);
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Slang] %s:%s could not be validated: %s", owner->GetCache().GetSourcePath().c_str(),
                    owner->GetEntryName().c_str(), e.what());
        }
    }
} // namespace pe

#endif
