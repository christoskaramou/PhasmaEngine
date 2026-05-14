#include "PipelineCreation.h"

#include "Device.h"
#include "DxilCacheHash.h"
#include "FormatMap.h"
#include "PipelineLayout.h"
#include "Wgsl.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12RhiImpl.h"
#include "Base/Path.h"
#include "dxcapi.h"
#endif

#include <exception>

namespace pwgpu
{
    namespace
    {
        PipelineCreationResult InternalError(std::string message)
        {
            PipelineCreationResult result;
            result.errorType = WGPUErrorType_Internal;
            result.message = std::move(message);
            return result;
        }

        PeGraphicsApi DeviceApi(WGPUDeviceImpl *device)
        {
            return (device && device->rhi) ? device->rhi->GetApi() : pe::RHII.GetApi();
        }

        PipelineCreationResult BackendUnsupported(const char *operation, WGPUDeviceImpl *device)
        {
            return InternalError(std::string(operation) + ": " +
                                 PeGraphicsApiName(DeviceApi(device)) +
                                 " backend pipeline creation is not implemented");
        }

        vk::ShaderModule CreateShaderModule(vk::Device vkDev, const std::vector<uint32_t> &spirv)
        {
            vk::ShaderModuleCreateInfo ci{};
            ci.codeSize = spirv.size() * sizeof(uint32_t);
            ci.pCode = spirv.data();
            return vkDev.createShaderModule(ci);
        }

#if defined(PE_WIN32)
        std::wstring Utf8ToWide(const std::string &text)
        {
            if (text.empty())
                return {};

            int sizeNeeded =
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            if (sizeNeeded <= 0)
                return {};

            std::wstring wide(static_cast<size_t>(sizeNeeded), 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), sizeNeeded);
            wide.resize(static_cast<size_t>(sizeNeeded - 1));
            return wide;
        }

        std::string DxcErrors(IDxcResult *result)
        {
            if (!result)
                return {};

            Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            if (errors && errors->GetStringLength() > 0)
                return errors->GetStringPointer();
            return {};
        }

        void BuildGeneratedHlslDxcArgs(const char *entryPoint,
                                       const wchar_t *profile,
                                       std::vector<std::wstring> &ownedArgs,
                                       std::vector<LPCWSTR> &args)
        {
            ownedArgs.clear();
            args.clear();
            ownedArgs.reserve(8);
            ownedArgs.emplace_back(L"-T");
            ownedArgs.emplace_back(profile ? profile : L"");
            ownedArgs.emplace_back(L"-E");
            ownedArgs.emplace_back(Utf8ToWide(entryPoint ? entryPoint : ""));
            ownedArgs.emplace_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);

#if PE_DEBUG_MODE
            ownedArgs.emplace_back(DXC_ARG_DEBUG);
#else
            ownedArgs.emplace_back(L"-Qstrip_debug");
            ownedArgs.emplace_back(L"-O3");
#endif

            args.reserve(ownedArgs.size());
            for (const std::wstring &arg : ownedArgs)
                args.push_back(arg.c_str());
        }

        std::filesystem::path Dx12WebGPUDxilCachePath(const std::string &key)
        {
            return std::filesystem::path(pe::Path::ExecutablePath()) /
                   "ShaderCache" /
                   "_dxil" /
                   ("webgpu_" + key + ".dxil");
        }

        bool ReadDxilFile(const std::filesystem::path &path, std::vector<uint8_t> &outDxil)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
                return false;

            const std::streamoff size = file.tellg();
            if (size <= 0)
                return false;

            outDxil.resize(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char *>(outDxil.data()), static_cast<std::streamsize>(size));
            if (!file)
            {
                outDxil.clear();
                return false;
            }
            return true;
        }

        void WriteDxilFile(const std::filesystem::path &path, const std::vector<uint8_t> &dxil)
        {
            if (dxil.empty())
                return;

            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
                return;

            std::filesystem::path tempPath = path;
            tempPath += ".tmp.";
            tempPath += std::to_string(GetCurrentProcessId());
            tempPath += ".";
            tempPath += std::to_string(GetCurrentThreadId());

            {
                std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                    return;
                file.write(reinterpret_cast<const char *>(dxil.data()), static_cast<std::streamsize>(dxil.size()));
                if (!file)
                {
                    file.close();
                    std::filesystem::remove(tempPath, ec);
                    return;
                }
            }

            std::filesystem::rename(tempPath, path, ec);
            if (ec)
            {
                std::filesystem::remove(tempPath, ec);
                return;
            }
        }

        struct Dx12WebGPUShaderCache
        {
            bool GetDxcObjects(Microsoft::WRL::ComPtr<IDxcUtils> &outUtils,
                               Microsoft::WRL::ComPtr<IDxcCompiler3> &outCompiler,
                               std::string &outVersion,
                               std::string &error)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!utils)
                {
                    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
                    if (FAILED(hr))
                    {
                        error = "failed to create IDxcUtils";
                        return false;
                    }
                }

                if (!compiler)
                {
                    HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
                    if (FAILED(hr))
                    {
                        error = "failed to create IDxcCompiler3";
                        return false;
                    }

                    dxcVersion = "dxc-version-unavailable";
                    Microsoft::WRL::ComPtr<IDxcVersionInfo> versionInfo;
                    if (SUCCEEDED(compiler.As(&versionInfo)) && versionInfo)
                    {
                        UINT32 major = 0;
                        UINT32 minor = 0;
                        UINT32 flags = 0;
                        if (SUCCEEDED(versionInfo->GetVersion(&major, &minor)) &&
                            SUCCEEDED(versionInfo->GetFlags(&flags)))
                        {
                            dxcVersion = "dxc-" + std::to_string(major) + "." + std::to_string(minor) +
                                         "-flags-" + std::to_string(flags);
                        }
                    }
                }

                outUtils = utils;
                outCompiler = compiler;
                outVersion = dxcVersion;
                return true;
            }

            bool TryLoad(const std::string &key, std::vector<uint8_t> &outDxil)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it = memoryDxil.find(key);
                    if (it != memoryDxil.end())
                    {
                        outDxil = it->second;
                        return true;
                    }
                }

                std::vector<uint8_t> diskDxil;
                if (!ReadDxilFile(Dx12WebGPUDxilCachePath(key), diskDxil))
                    return false;

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    memoryDxil[key] = diskDxil;
                }
                outDxil = std::move(diskDxil);
                return true;
            }

            void Store(const std::string &key, const std::vector<uint8_t> &dxil)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    memoryDxil[key] = dxil;
                }
                WriteDxilFile(Dx12WebGPUDxilCachePath(key), dxil);
            }

            std::mutex mutex;
            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
            std::string dxcVersion = "dxc-uninitialized";
            std::unordered_map<std::string, std::vector<uint8_t>> memoryDxil;
        };

        Dx12WebGPUShaderCache &GeneratedShaderCache()
        {
            static Dx12WebGPUShaderCache cache;
            return cache;
        }

        std::string BuildDx12WebGPUDxilCacheKey(const std::vector<uint32_t> &spirv,
                                                const char *entryPoint,
                                                const char *stage,
                                                const wchar_t *profile,
                                                const std::string &dxcVersion)
        {
            static constexpr uint32_t kDx12WebGPUDxilCacheVersion = 1;
            static constexpr uint32_t kDx12WebGPUCbvRegisterShiftVersion = 2;
            static constexpr std::string_view kDx12WebGPUNagaBridgeVersion =
                "naga-29.0.1-spv-to-hlsl-semantic-cbv-shift-sampler-cap64-vector-select2-demote-kill";

            DxilCacheSha256 hash;
            HashDxilCacheString(hash, "PhasmaWebGPU.DX12.DXIL");
            hash.UpdateUint32(kDx12WebGPUDxilCacheVersion);
            hash.UpdateUint32(kDx12WebGPUCbvRegisterShiftVersion);
            HashDxilCacheString(hash, kDx12WebGPUNagaBridgeVersion);
            HashDxilCacheString(hash, dxcVersion);
            HashDxilCacheString(hash, stage ? stage : "");
            HashDxilCacheString(hash, entryPoint ? entryPoint : "");
            HashDxilCacheWideString(hash, profile ? std::wstring_view(profile) : std::wstring_view());

            std::vector<std::wstring> ownedArgs;
            std::vector<LPCWSTR> args;
            BuildGeneratedHlslDxcArgs(entryPoint, profile, ownedArgs, args);
            hash.UpdateUint64(ownedArgs.size());
            for (const std::wstring &arg : ownedArgs)
                HashDxilCacheWideString(hash, arg);

            hash.UpdateUint64(spirv.size());
            for (uint32_t word : spirv)
                hash.UpdateUint32(word);
            return DxilCacheHexString(hash.Final());
        }

        bool CompileGeneratedHlslToDxil(IDxcUtils *utils,
                                        IDxcCompiler3 *compiler,
                                        const std::string &hlsl,
                                        const char *entryPoint,
                                        const wchar_t *profile,
                                        std::vector<uint8_t> &outDxil,
                                        std::string &error)
        {
            if (!utils || !compiler)
            {
                error = "DXC compiler is unavailable";
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
            HRESULT hr = utils->CreateBlob(hlsl.data(), static_cast<uint32_t>(hlsl.size()), CP_UTF8, &source);
            if (FAILED(hr))
            {
                error = "failed to create DXC source blob";
                return false;
            }

            DxcBuffer sourceBuffer{};
            sourceBuffer.Ptr = source->GetBufferPointer();
            sourceBuffer.Size = source->GetBufferSize();
            sourceBuffer.Encoding = 0;

            std::vector<std::wstring> ownedArgs;
            std::vector<LPCWSTR> args;
            BuildGeneratedHlslDxcArgs(entryPoint, profile, ownedArgs, args);

            Microsoft::WRL::ComPtr<IDxcResult> result;
            hr = compiler->Compile(&sourceBuffer,
                                   args.data(),
                                   static_cast<uint32_t>(args.size()),
                                   nullptr,
                                   IID_PPV_ARGS(&result));
            if (FAILED(hr) || !result)
            {
                error = "DXC compile call failed";
                return false;
            }

            HRESULT compileStatus = S_OK;
            result->GetStatus(&compileStatus);
            if (FAILED(compileStatus))
            {
                error = DxcErrors(result.Get());
                if (error.empty())
                    error = "DXC compilation failed";
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcBlob> object;
            result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            if (!object)
            {
                error = DxcErrors(result.Get());
                if (error.empty())
                    error = "DXC did not produce an object";
                return false;
            }

            const auto *bytes = static_cast<const uint8_t *>(object->GetBufferPointer());
            outDxil.assign(bytes, bytes + object->GetBufferSize());
            return true;
        }

        bool GetDx12WebGPUDxil(const std::vector<uint32_t> &spirv,
                               const char *entryPoint,
                               const char *stage,
                               const wchar_t *profile,
                               std::vector<uint8_t> &outDxil,
                               std::string &error)
        {
            auto &cache = GeneratedShaderCache();

            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
            std::string dxcVersion;
            if (!cache.GetDxcObjects(utils, compiler, dxcVersion, error))
                return false;

            const std::string key =
                BuildDx12WebGPUDxilCacheKey(spirv, entryPoint, stage, profile, dxcVersion);
            if (cache.TryLoad(key, outDxil))
                return true;

            const std::string hlsl = pwgpu::Wgsl::SpirvToHlsl(spirv, entryPoint ? entryPoint : "", stage);
            if (hlsl.empty())
            {
                error = "failed to translate SPIR-V to HLSL for DX12";
                const std::string bridgeError = pwgpu::Wgsl::LastSpirvToHlslError();
                if (!bridgeError.empty())
                    error += ": " + bridgeError;
                return false;
            }

            if (!CompileGeneratedHlslToDxil(utils.Get(), compiler.Get(), hlsl, entryPoint, profile, outDxil, error))
                return false;

            cache.Store(key, outDxil);
            return true;
        }

        std::string Dx12InfoQueueTail(ID3D12Device *device)
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
            if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
                return {};

            const UINT64 count = infoQueue->GetNumStoredMessages();
            if (count == 0)
                return {};

            std::ostringstream out;
            const UINT64 first = count > 8 ? count - 8 : 0;
            for (UINT64 i = first; i < count; ++i)
            {
                SIZE_T messageLength = 0;
                if (FAILED(infoQueue->GetMessage(i, nullptr, &messageLength)) ||
                    messageLength == 0)
                    continue;

                std::vector<char> storage(messageLength);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (FAILED(infoQueue->GetMessage(i, message, &messageLength)) ||
                    !message->pDescription)
                    continue;

                if (out.tellp() > 0)
                    out << " | ";
                out << message->pDescription;
            }
            return out.str();
        }

        DXGI_FORMAT ToDxgiTextureFormat(WGPUTextureFormat format)
        {
            switch (format)
            {
            case WGPUTextureFormat_R8Unorm:
                return DXGI_FORMAT_R8_UNORM;
            case WGPUTextureFormat_R8Snorm:
                return DXGI_FORMAT_R8_SNORM;
            case WGPUTextureFormat_R8Uint:
                return DXGI_FORMAT_R8_UINT;
            case WGPUTextureFormat_R8Sint:
                return DXGI_FORMAT_R8_SINT;
            case WGPUTextureFormat_R16Uint:
                return DXGI_FORMAT_R16_UINT;
            case WGPUTextureFormat_R16Sint:
                return DXGI_FORMAT_R16_SINT;
            case WGPUTextureFormat_R16Float:
                return DXGI_FORMAT_R16_FLOAT;
            case WGPUTextureFormat_RG8Unorm:
                return DXGI_FORMAT_R8G8_UNORM;
            case WGPUTextureFormat_RG8Snorm:
                return DXGI_FORMAT_R8G8_SNORM;
            case WGPUTextureFormat_RG8Uint:
                return DXGI_FORMAT_R8G8_UINT;
            case WGPUTextureFormat_RG8Sint:
                return DXGI_FORMAT_R8G8_SINT;
            case WGPUTextureFormat_R32Float:
                return DXGI_FORMAT_R32_FLOAT;
            case WGPUTextureFormat_R32Uint:
                return DXGI_FORMAT_R32_UINT;
            case WGPUTextureFormat_R32Sint:
                return DXGI_FORMAT_R32_SINT;
            case WGPUTextureFormat_RG16Uint:
                return DXGI_FORMAT_R16G16_UINT;
            case WGPUTextureFormat_RG16Sint:
                return DXGI_FORMAT_R16G16_SINT;
            case WGPUTextureFormat_RG16Float:
                return DXGI_FORMAT_R16G16_FLOAT;
            case WGPUTextureFormat_RGBA8Unorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case WGPUTextureFormat_RGBA8UnormSrgb:
                return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case WGPUTextureFormat_RGBA8Snorm:
                return DXGI_FORMAT_R8G8B8A8_SNORM;
            case WGPUTextureFormat_RGBA8Uint:
                return DXGI_FORMAT_R8G8B8A8_UINT;
            case WGPUTextureFormat_RGBA8Sint:
                return DXGI_FORMAT_R8G8B8A8_SINT;
            case WGPUTextureFormat_BGRA8Unorm:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case WGPUTextureFormat_BGRA8UnormSrgb:
                return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case WGPUTextureFormat_RGB10A2Unorm:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case WGPUTextureFormat_RG11B10Ufloat:
                return DXGI_FORMAT_R11G11B10_FLOAT;
            case WGPUTextureFormat_RG32Float:
                return DXGI_FORMAT_R32G32_FLOAT;
            case WGPUTextureFormat_RG32Uint:
                return DXGI_FORMAT_R32G32_UINT;
            case WGPUTextureFormat_RG32Sint:
                return DXGI_FORMAT_R32G32_SINT;
            case WGPUTextureFormat_RGBA16Uint:
                return DXGI_FORMAT_R16G16B16A16_UINT;
            case WGPUTextureFormat_RGBA16Sint:
                return DXGI_FORMAT_R16G16B16A16_SINT;
            case WGPUTextureFormat_RGBA16Float:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case WGPUTextureFormat_RGBA32Float:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case WGPUTextureFormat_RGBA32Uint:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            case WGPUTextureFormat_RGBA32Sint:
                return DXGI_FORMAT_R32G32B32A32_SINT;
            case WGPUTextureFormat_Depth16Unorm:
                return DXGI_FORMAT_D16_UNORM;
            case WGPUTextureFormat_Depth24Plus:
            case WGPUTextureFormat_Depth32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case WGPUTextureFormat_Depth24PlusStencil8:
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case WGPUTextureFormat_Depth32FloatStencil8:
                return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        DXGI_FORMAT VertexFormatToDxgi(WGPUVertexFormat format)
        {
            switch (format)
            {
            case WGPUVertexFormat_Uint8:
                return DXGI_FORMAT_R8_UINT;
            case WGPUVertexFormat_Uint8x2:
                return DXGI_FORMAT_R8G8_UINT;
            case WGPUVertexFormat_Uint8x4:
                return DXGI_FORMAT_R8G8B8A8_UINT;
            case WGPUVertexFormat_Sint8:
                return DXGI_FORMAT_R8_SINT;
            case WGPUVertexFormat_Sint8x2:
                return DXGI_FORMAT_R8G8_SINT;
            case WGPUVertexFormat_Sint8x4:
                return DXGI_FORMAT_R8G8B8A8_SINT;
            case WGPUVertexFormat_Unorm8:
                return DXGI_FORMAT_R8_UNORM;
            case WGPUVertexFormat_Unorm8x2:
                return DXGI_FORMAT_R8G8_UNORM;
            case WGPUVertexFormat_Unorm8x4:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case WGPUVertexFormat_Snorm8:
                return DXGI_FORMAT_R8_SNORM;
            case WGPUVertexFormat_Snorm8x2:
                return DXGI_FORMAT_R8G8_SNORM;
            case WGPUVertexFormat_Snorm8x4:
                return DXGI_FORMAT_R8G8B8A8_SNORM;
            case WGPUVertexFormat_Uint16:
                return DXGI_FORMAT_R16_UINT;
            case WGPUVertexFormat_Uint16x2:
                return DXGI_FORMAT_R16G16_UINT;
            case WGPUVertexFormat_Uint16x4:
                return DXGI_FORMAT_R16G16B16A16_UINT;
            case WGPUVertexFormat_Sint16:
                return DXGI_FORMAT_R16_SINT;
            case WGPUVertexFormat_Sint16x2:
                return DXGI_FORMAT_R16G16_SINT;
            case WGPUVertexFormat_Sint16x4:
                return DXGI_FORMAT_R16G16B16A16_SINT;
            case WGPUVertexFormat_Unorm16:
                return DXGI_FORMAT_R16_UNORM;
            case WGPUVertexFormat_Unorm16x2:
                return DXGI_FORMAT_R16G16_UNORM;
            case WGPUVertexFormat_Unorm16x4:
                return DXGI_FORMAT_R16G16B16A16_UNORM;
            case WGPUVertexFormat_Snorm16:
                return DXGI_FORMAT_R16_SNORM;
            case WGPUVertexFormat_Snorm16x2:
                return DXGI_FORMAT_R16G16_SNORM;
            case WGPUVertexFormat_Snorm16x4:
                return DXGI_FORMAT_R16G16B16A16_SNORM;
            case WGPUVertexFormat_Float16:
                return DXGI_FORMAT_R16_FLOAT;
            case WGPUVertexFormat_Float16x2:
                return DXGI_FORMAT_R16G16_FLOAT;
            case WGPUVertexFormat_Float16x4:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case WGPUVertexFormat_Float32:
                return DXGI_FORMAT_R32_FLOAT;
            case WGPUVertexFormat_Float32x2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case WGPUVertexFormat_Float32x3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case WGPUVertexFormat_Float32x4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case WGPUVertexFormat_Uint32:
                return DXGI_FORMAT_R32_UINT;
            case WGPUVertexFormat_Uint32x2:
                return DXGI_FORMAT_R32G32_UINT;
            case WGPUVertexFormat_Uint32x3:
                return DXGI_FORMAT_R32G32B32_UINT;
            case WGPUVertexFormat_Uint32x4:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            case WGPUVertexFormat_Sint32:
                return DXGI_FORMAT_R32_SINT;
            case WGPUVertexFormat_Sint32x2:
                return DXGI_FORMAT_R32G32_SINT;
            case WGPUVertexFormat_Sint32x3:
                return DXGI_FORMAT_R32G32B32_SINT;
            case WGPUVertexFormat_Sint32x4:
                return DXGI_FORMAT_R32G32B32A32_SINT;
            case WGPUVertexFormat_Unorm10_10_10_2:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case WGPUVertexFormat_Unorm8x4BGRA:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        D3D12_PRIMITIVE_TOPOLOGY_TYPE ToDx12TopologyType(WGPUPrimitiveTopology topology)
        {
            switch (topology)
            {
            case WGPUPrimitiveTopology_PointList:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            case WGPUPrimitiveTopology_LineList:
            case WGPUPrimitiveTopology_LineStrip:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            case WGPUPrimitiveTopology_TriangleList:
            case WGPUPrimitiveTopology_TriangleStrip:
            default:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            }
        }

        D3D12_CULL_MODE ToDx12CullMode(WGPUCullMode mode)
        {
            switch (mode)
            {
            case WGPUCullMode_Front:
                return D3D12_CULL_MODE_FRONT;
            case WGPUCullMode_Back:
                return D3D12_CULL_MODE_BACK;
            case WGPUCullMode_None:
            default:
                return D3D12_CULL_MODE_NONE;
            }
        }

        D3D12_COMPARISON_FUNC ToDx12Compare(WGPUCompareFunction compare)
        {
            switch (compare)
            {
            case WGPUCompareFunction_Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case WGPUCompareFunction_Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case WGPUCompareFunction_Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case WGPUCompareFunction_LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case WGPUCompareFunction_Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case WGPUCompareFunction_NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case WGPUCompareFunction_GreaterEqual:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case WGPUCompareFunction_Always:
            default:
                return D3D12_COMPARISON_FUNC_ALWAYS;
            }
        }

        D3D12_STENCIL_OP ToDx12StencilOp(WGPUStencilOperation op)
        {
            switch (op)
            {
            case WGPUStencilOperation_Zero:
                return D3D12_STENCIL_OP_ZERO;
            case WGPUStencilOperation_Replace:
                return D3D12_STENCIL_OP_REPLACE;
            case WGPUStencilOperation_Invert:
                return D3D12_STENCIL_OP_INVERT;
            case WGPUStencilOperation_IncrementClamp:
                return D3D12_STENCIL_OP_INCR_SAT;
            case WGPUStencilOperation_DecrementClamp:
                return D3D12_STENCIL_OP_DECR_SAT;
            case WGPUStencilOperation_IncrementWrap:
                return D3D12_STENCIL_OP_INCR;
            case WGPUStencilOperation_DecrementWrap:
                return D3D12_STENCIL_OP_DECR;
            case WGPUStencilOperation_Keep:
            default:
                return D3D12_STENCIL_OP_KEEP;
            }
        }

        D3D12_BLEND ToDx12BlendFactor(WGPUBlendFactor factor)
        {
            switch (factor)
            {
            case WGPUBlendFactor_Zero:
                return D3D12_BLEND_ZERO;
            case WGPUBlendFactor_One:
                return D3D12_BLEND_ONE;
            case WGPUBlendFactor_Src:
                return D3D12_BLEND_SRC_COLOR;
            case WGPUBlendFactor_OneMinusSrc:
                return D3D12_BLEND_INV_SRC_COLOR;
            case WGPUBlendFactor_SrcAlpha:
                return D3D12_BLEND_SRC_ALPHA;
            case WGPUBlendFactor_OneMinusSrcAlpha:
                return D3D12_BLEND_INV_SRC_ALPHA;
            case WGPUBlendFactor_Dst:
                return D3D12_BLEND_DEST_COLOR;
            case WGPUBlendFactor_OneMinusDst:
                return D3D12_BLEND_INV_DEST_COLOR;
            case WGPUBlendFactor_DstAlpha:
                return D3D12_BLEND_DEST_ALPHA;
            case WGPUBlendFactor_OneMinusDstAlpha:
                return D3D12_BLEND_INV_DEST_ALPHA;
            case WGPUBlendFactor_SrcAlphaSaturated:
                return D3D12_BLEND_SRC_ALPHA_SAT;
            case WGPUBlendFactor_Constant:
                return D3D12_BLEND_BLEND_FACTOR;
            case WGPUBlendFactor_OneMinusConstant:
                return D3D12_BLEND_INV_BLEND_FACTOR;
            case WGPUBlendFactor_Src1:
                return D3D12_BLEND_SRC1_COLOR;
            case WGPUBlendFactor_OneMinusSrc1:
                return D3D12_BLEND_INV_SRC1_COLOR;
            case WGPUBlendFactor_Src1Alpha:
                return D3D12_BLEND_SRC1_ALPHA;
            case WGPUBlendFactor_OneMinusSrc1Alpha:
                return D3D12_BLEND_INV_SRC1_ALPHA;
            default:
                return D3D12_BLEND_ONE;
            }
        }

        D3D12_BLEND_OP ToDx12BlendOp(WGPUBlendOperation op)
        {
            switch (op)
            {
            case WGPUBlendOperation_Subtract:
                return D3D12_BLEND_OP_SUBTRACT;
            case WGPUBlendOperation_ReverseSubtract:
                return D3D12_BLEND_OP_REV_SUBTRACT;
            case WGPUBlendOperation_Min:
                return D3D12_BLEND_OP_MIN;
            case WGPUBlendOperation_Max:
                return D3D12_BLEND_OP_MAX;
            case WGPUBlendOperation_Add:
            default:
                return D3D12_BLEND_OP_ADD;
            }
        }

        D3D12_RENDER_TARGET_BLEND_DESC ToDx12BlendAttachment(const WGPUColorTargetState &target)
        {
            D3D12_RENDER_TARGET_BLEND_DESC attachment{};
            attachment.BlendEnable = target.blend ? TRUE : FALSE;
            attachment.LogicOpEnable = FALSE;
            attachment.SrcBlend = D3D12_BLEND_ONE;
            attachment.DestBlend = D3D12_BLEND_ZERO;
            attachment.BlendOp = D3D12_BLEND_OP_ADD;
            attachment.SrcBlendAlpha = D3D12_BLEND_ONE;
            attachment.DestBlendAlpha = D3D12_BLEND_ZERO;
            attachment.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            attachment.LogicOp = D3D12_LOGIC_OP_NOOP;
            attachment.RenderTargetWriteMask = static_cast<UINT8>(target.writeMask);

            if (target.blend)
            {
                WGPUBlendFactor colorSrc = target.blend->color.srcFactor;
                WGPUBlendFactor colorDst = target.blend->color.dstFactor;
                WGPUBlendOperation colorOp = target.blend->color.operation;
                WGPUBlendFactor alphaSrc = target.blend->alpha.srcFactor;
                WGPUBlendFactor alphaDst = target.blend->alpha.dstFactor;
                WGPUBlendOperation alphaOp = target.blend->alpha.operation;
                if (colorSrc == WGPUBlendFactor(0))
                    colorSrc = WGPUBlendFactor_One;
                if (colorDst == WGPUBlendFactor(0))
                    colorDst = WGPUBlendFactor_Zero;
                if (colorOp == WGPUBlendOperation(0))
                    colorOp = WGPUBlendOperation_Add;
                if (alphaSrc == WGPUBlendFactor(0))
                    alphaSrc = WGPUBlendFactor_One;
                if (alphaDst == WGPUBlendFactor(0))
                    alphaDst = WGPUBlendFactor_Zero;
                if (alphaOp == WGPUBlendOperation(0))
                    alphaOp = WGPUBlendOperation_Add;

                attachment.SrcBlend = ToDx12BlendFactor(colorSrc);
                attachment.DestBlend = ToDx12BlendFactor(colorDst);
                attachment.BlendOp = ToDx12BlendOp(colorOp);
                attachment.SrcBlendAlpha = ToDx12BlendFactor(alphaSrc);
                attachment.DestBlendAlpha = ToDx12BlendFactor(alphaDst);
                attachment.BlendOpAlpha = ToDx12BlendOp(alphaOp);
            }

            return attachment;
        }
#endif

        WGPUCullMode ResolveCullMode(WGPUCullMode mode)
        {
            return mode == WGPUCullMode(0) ? WGPUCullMode_None : mode;
        }

        WGPUFrontFace ResolveFrontFace(WGPUFrontFace face)
        {
            return face == WGPUFrontFace(0) ? WGPUFrontFace_CCW : face;
        }

        WGPUBlendOperation ResolveBlendOperation(WGPUBlendOperation op)
        {
            return op == WGPUBlendOperation(0) ? WGPUBlendOperation_Add : op;
        }

        WGPUBlendFactor ResolveBlendSrcFactor(WGPUBlendFactor factor)
        {
            return factor == WGPUBlendFactor(0) ? WGPUBlendFactor_One : factor;
        }

        WGPUBlendFactor ResolveBlendDstFactor(WGPUBlendFactor factor)
        {
            return factor == WGPUBlendFactor(0) ? WGPUBlendFactor_Zero : factor;
        }

        vk::PipelineColorBlendAttachmentState ToVkBlendAttachment(const WGPUColorTargetState &target)
        {
            vk::PipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask =
                vk::ColorComponentFlags(static_cast<uint32_t>(target.writeMask));

            if (!target.blend)
                return attachment;

            attachment.blendEnable = VK_TRUE;
            attachment.colorBlendOp = ToVkBlendOp(ResolveBlendOperation(target.blend->color.operation));
            attachment.srcColorBlendFactor =
                ToVkBlendFactor(ResolveBlendSrcFactor(target.blend->color.srcFactor));
            attachment.dstColorBlendFactor =
                ToVkBlendFactor(ResolveBlendDstFactor(target.blend->color.dstFactor));
            attachment.alphaBlendOp = ToVkBlendOp(ResolveBlendOperation(target.blend->alpha.operation));
            attachment.srcAlphaBlendFactor =
                ToVkBlendFactor(ResolveBlendSrcFactor(target.blend->alpha.srcFactor));
            attachment.dstAlphaBlendFactor =
                ToVkBlendFactor(ResolveBlendDstFactor(target.blend->alpha.dstFactor));
            return attachment;
        }
    } // namespace

    PipelineCreationResult CreateWebGPUComputePipelineBackend(const ComputePipelineBackendDesc &desc)
    {
        if (!desc.device || !desc.layout || !desc.spirv || !desc.entryPoint)
        {
            return InternalError("createComputePipeline: invalid backend creation descriptor");
        }

        switch (DeviceApi(desc.device))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (desc.layout->backendLayout == 0)
                return InternalError("createComputePipeline: invalid Vulkan pipeline layout");

            auto vkDev = pe::VulkanRhi::Device();
            vk::ShaderModule shaderModule{};
            try
            {
                shaderModule = CreateShaderModule(vkDev, *desc.spirv);
            }
            catch (...)
            {
                return InternalError("createComputePipeline: failed to create VkShaderModule");
            }

            vk::ComputePipelineCreateInfo cpci{};
            if (desc.layout->bindingModel == WGPUBindingModel::DescriptorBuffer)
                cpci.flags |= vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
            cpci.stage.stage = vk::ShaderStageFlagBits::eCompute;
            cpci.stage.module = shaderModule;
            cpci.stage.pName = desc.entryPoint;
            cpci.layout =
                vk::PipelineLayout{PeFromBackendHandle<VkPipelineLayout>(desc.layout->backendLayout)};

            vk::Pipeline pipeline{};
            try
            {
                auto result = vkDev.createComputePipeline(nullptr, cpci);
                if (result.result != vk::Result::eSuccess)
                {
                    vkDev.destroyShaderModule(shaderModule);
                    return InternalError("createComputePipeline: vkCreateComputePipelines failed");
                }
                pipeline = result.value;
            }
            catch (...)
            {
                vkDev.destroyShaderModule(shaderModule);
                return InternalError("createComputePipeline: vkCreateComputePipelines threw");
            }

            vkDev.destroyShaderModule(shaderModule);

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(static_cast<VkPipeline>(pipeline));
            return result;
        }
        case PE_GRAPHICS_API_DX12:
        {
#if defined(PE_WIN32)
            auto *rhi = static_cast<pe::Dx12RhiImpl *>(desc.device->rhi ? desc.device->rhi->GetImpl() : nullptr);
            if (!rhi || !rhi->GetDevice() || !rhi->GetSharedRootSig())
                return InternalError("createComputePipeline: DX12 RHI device/root signature is unavailable");

            std::vector<uint8_t> dxil;
            std::string shaderError;
            if (!GetDx12WebGPUDxil(*desc.spirv, desc.entryPoint, "compute", L"cs_6_6", dxil, shaderError))
            {
                std::string message = "createComputePipeline: failed to prepare DX12 DXIL";
                if (!shaderError.empty())
                    message += ": " + shaderError;
                return InternalError(std::move(message));
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC cpci{};
            cpci.pRootSignature = rhi->GetSharedRootSig()->Get();
            cpci.CS.pShaderBytecode = dxil.data();
            cpci.CS.BytecodeLength = dxil.size();

            ID3D12PipelineState *pipeline = nullptr;
            HRESULT hr = rhi->GetDevice()->CreateComputePipelineState(&cpci, IID_PPV_ARGS(&pipeline));
            if (FAILED(hr) || !pipeline)
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "createComputePipeline: CreateComputePipelineState failed (0x%08X)",
                              static_cast<unsigned>(hr));
                return InternalError(buf);
            }

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(pipeline);
            return result;
#else
            return InternalError("createComputePipeline: DX12 backend is Windows-only");
#endif
        }
        default:
            return BackendUnsupported("createComputePipeline", desc.device);
        }
    }

    PipelineCreationResult CreateWebGPURenderPipelineBackend(const RenderPipelineBackendDesc &desc)
    {
        if (!desc.device || !desc.layout || !desc.descriptor || !desc.vertexSpirv ||
            !desc.vertexEntryPoint)
        {
            return InternalError("createRenderPipeline: invalid backend creation descriptor");
        }
        if (desc.hasFragment && (!desc.fragmentSpirv || !desc.fragmentEntryPoint))
            return InternalError("createRenderPipeline: invalid fragment backend creation descriptor");

        switch (DeviceApi(desc.device))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            if (desc.layout->backendLayout == 0)
                return InternalError("createRenderPipeline: invalid Vulkan pipeline layout");

            auto vkDev = pe::VulkanRhi::Device();

            std::vector<vk::PipelineShaderStageCreateInfo> stages;
            std::vector<vk::ShaderModule> tempModules;
            stages.reserve(2);

            vk::ShaderModule vkVertModule{};
            try
            {
                vkVertModule = CreateShaderModule(vkDev, *desc.vertexSpirv);
            }
            catch (...)
            {
                return InternalError("createRenderPipeline: failed to create vertex VkShaderModule");
            }
            tempModules.push_back(vkVertModule);

            vk::PipelineShaderStageCreateInfo vertStage{};
            vertStage.stage = vk::ShaderStageFlagBits::eVertex;
            vertStage.module = vkVertModule;
            vertStage.pName = desc.vertexEntryPoint;
            stages.push_back(vertStage);

            if (desc.hasFragment)
            {
                vk::ShaderModule vkFragModule{};
                try
                {
                    vkFragModule = CreateShaderModule(vkDev, *desc.fragmentSpirv);
                }
                catch (...)
                {
                    for (auto m : tempModules)
                        vkDev.destroyShaderModule(m);
                    return InternalError("createRenderPipeline: failed to create fragment VkShaderModule");
                }
                tempModules.push_back(vkFragModule);

                vk::PipelineShaderStageCreateInfo fragStage{};
                fragStage.stage = vk::ShaderStageFlagBits::eFragment;
                fragStage.module = vkFragModule;
                fragStage.pName = desc.fragmentEntryPoint;
                stages.push_back(fragStage);
            }

            const WGPURenderPipelineDescriptor &descriptor = *desc.descriptor;

            std::vector<vk::VertexInputBindingDescription> vertBindings;
            std::vector<vk::VertexInputAttributeDescription> vertAttrs;

            for (uint32_t slot = 0; slot < descriptor.vertex.bufferCount; ++slot)
            {
                const auto &vbuf = descriptor.vertex.buffers[slot];
                if (vbuf.attributeCount == 0 && vbuf.arrayStride == 0)
                    continue;

                vk::VertexInputBindingDescription binding{};
                binding.binding = slot;
                binding.stride = static_cast<uint32_t>(vbuf.arrayStride);
                binding.inputRate = (vbuf.stepMode == WGPUVertexStepMode_Instance)
                                        ? vk::VertexInputRate::eInstance
                                        : vk::VertexInputRate::eVertex;
                vertBindings.push_back(binding);

                for (size_t a = 0; a < vbuf.attributeCount; ++a)
                {
                    vk::VertexInputAttributeDescription attr{};
                    attr.location = vbuf.attributes[a].shaderLocation;
                    attr.binding = slot;
                    attr.format =
                        static_cast<vk::Format>(VertexFormatToVk(vbuf.attributes[a].format));
                    attr.offset = static_cast<uint32_t>(vbuf.attributes[a].offset);
                    vertAttrs.push_back(attr);
                }
            }

            vk::PipelineVertexInputStateCreateInfo vertexInputState{};
            vertexInputState.vertexBindingDescriptionCount =
                static_cast<uint32_t>(vertBindings.size());
            vertexInputState.pVertexBindingDescriptions = vertBindings.data();
            vertexInputState.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(vertAttrs.size());
            vertexInputState.pVertexAttributeDescriptions = vertAttrs.data();

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = ToVkTopology(desc.primitiveTopology);
            inputAssembly.primitiveRestartEnable =
                IsStripTopology(desc.primitiveTopology) ? VK_TRUE : VK_FALSE;

            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterState{};
            rasterState.depthClampEnable = VK_FALSE;
            rasterState.rasterizerDiscardEnable =
                (!desc.hasFragment && !desc.hasDepthStencil) ? VK_TRUE : VK_FALSE;
            rasterState.polygonMode = vk::PolygonMode::eFill;
            rasterState.cullMode = ToVkCullMode(ResolveCullMode(descriptor.primitive.cullMode));
            rasterState.frontFace =
                (ResolveFrontFace(descriptor.primitive.frontFace) == WGPUFrontFace_CW)
                    ? vk::FrontFace::eClockwise
                    : vk::FrontFace::eCounterClockwise;
            rasterState.lineWidth = 1.0f;

            if (desc.hasDepthStencil)
            {
                const auto &ds = *descriptor.depthStencil;
                bool hasBias = (ds.depthBias != 0 ||
                                ds.depthBiasSlopeScale != 0.0f ||
                                ds.depthBiasClamp != 0.0f);
                rasterState.depthBiasEnable = hasBias ? VK_TRUE : VK_FALSE;
                rasterState.depthBiasConstantFactor = static_cast<float>(ds.depthBias);
                rasterState.depthBiasSlopeFactor = ds.depthBiasSlopeScale;
                rasterState.depthBiasClamp = ds.depthBiasClamp;
            }

            vk::PipelineRasterizationDepthClipStateCreateInfoEXT depthClipState{};
            const bool needsUnclippedDepth = descriptor.primitive.unclippedDepth;
            const bool needsFragDepthClamp =
                desc.fragmentWritesFragDepth &&
                desc.device->supportsDepthClamp &&
                desc.device->supportsDepthClipEnable;
            if (needsUnclippedDepth || needsFragDepthClamp)
            {
                depthClipState.depthClipEnable = needsUnclippedDepth ? VK_FALSE : VK_TRUE;
                depthClipState.pNext = rasterState.pNext;
                rasterState.pNext = &depthClipState;
                rasterState.depthClampEnable = VK_TRUE;
            }

            vk::PipelineMultisampleStateCreateInfo multisampleState{};
            multisampleState.rasterizationSamples = ToVkSampleCount(desc.sampleCount);
            uint32_t msMask = descriptor.multisample.mask;
            multisampleState.pSampleMask = &msMask;
            multisampleState.alphaToCoverageEnable =
                descriptor.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;

            vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
            if (desc.hasDepthStencil)
            {
                const auto &ds = *descriptor.depthStencil;
                bool hasDepthAspect = HasDepthAspect(ds.format);

                depthStencilState.depthTestEnable = hasDepthAspect ? VK_TRUE : VK_FALSE;
                depthStencilState.depthWriteEnable =
                    (ds.depthWriteEnabled == WGPUOptionalBool_True) ? VK_TRUE : VK_FALSE;

                auto dc = ds.depthCompare;
                if (dc == WGPUCompareFunction_Undefined)
                    dc = WGPUCompareFunction_Always;
                depthStencilState.depthCompareOp = ToVkCompareOp(dc);

                bool hasStencilAspect = HasStencilAspect(ds.format);
                depthStencilState.stencilTestEnable = hasStencilAspect ? VK_TRUE : VK_FALSE;

                auto mapFace = [](const WGPUStencilFaceState &face,
                                  uint32_t readMask,
                                  uint32_t writeMask) -> vk::StencilOpState
                {
                    vk::StencilOpState s{};
                    auto cmp = face.compare;
                    if (cmp == WGPUCompareFunction_Undefined || cmp == WGPUCompareFunction(0))
                        cmp = WGPUCompareFunction_Always;
                    s.compareOp = ToVkCompareOp(cmp);

                    auto fail = face.failOp;
                    if (fail == WGPUStencilOperation(0))
                        fail = WGPUStencilOperation_Keep;
                    s.failOp = ToVkStencilOp(fail);

                    auto dfail = face.depthFailOp;
                    if (dfail == WGPUStencilOperation(0))
                        dfail = WGPUStencilOperation_Keep;
                    s.depthFailOp = ToVkStencilOp(dfail);

                    auto pass = face.passOp;
                    if (pass == WGPUStencilOperation(0))
                        pass = WGPUStencilOperation_Keep;
                    s.passOp = ToVkStencilOp(pass);

                    s.compareMask = readMask;
                    s.writeMask = writeMask;
                    s.reference = 0;
                    return s;
                };

                uint32_t stencilRead = ds.stencilReadMask ? ds.stencilReadMask : 0xFFFFFFFFu;
                uint32_t stencilWrite = ds.stencilWriteMask ? ds.stencilWriteMask : 0xFFFFFFFFu;
                depthStencilState.front = mapFace(ds.stencilFront, stencilRead, stencilWrite);
                depthStencilState.back = mapFace(ds.stencilBack, stencilRead, stencilWrite);
            }

            std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
            std::vector<vk::Format> colorVkFormats;
            if (desc.hasFragment)
            {
                blendAttachments.reserve(descriptor.fragment->targetCount);
                colorVkFormats.reserve(descriptor.fragment->targetCount);
                for (size_t i = 0; i < descriptor.fragment->targetCount; ++i)
                {
                    const auto &target = descriptor.fragment->targets[i];
                    if (target.format == WGPUTextureFormat_Undefined)
                    {
                        blendAttachments.emplace_back();
                        colorVkFormats.push_back(vk::Format::eUndefined);
                    }
                    else
                    {
                        blendAttachments.push_back(ToVkBlendAttachment(target));
                        colorVkFormats.push_back(static_cast<vk::Format>(ToVkFormat(target.format)));
                    }
                }
            }

            vk::PipelineColorBlendStateCreateInfo colorBlendState{};
            colorBlendState.logicOpEnable = VK_FALSE;
            colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
            colorBlendState.pAttachments = blendAttachments.data();

            std::vector<vk::DynamicState> dynStates = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor,
            };
            if (desc.hasDepthStencil && HasStencilAspect(descriptor.depthStencil->format))
                dynStates.push_back(vk::DynamicState::eStencilReference);
            dynStates.push_back(vk::DynamicState::eBlendConstants);

            vk::PipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
            dynamicState.pDynamicStates = dynStates.data();

            vk::PipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorVkFormats.size());
            renderingInfo.pColorAttachmentFormats = colorVkFormats.data();
            if (desc.hasDepthStencil)
            {
                vk::Format dsVkFmt = static_cast<vk::Format>(
                    ResolveVkTextureFormat(descriptor.depthStencil->format,
                                           desc.device->resolvedDepth24Plus,
                                           desc.device->resolvedDepth24PlusStencil8,
                                           desc.device->resolvedStencil8));
                if (HasDepthAspect(descriptor.depthStencil->format))
                    renderingInfo.depthAttachmentFormat = dsVkFmt;
                if (HasStencilAspect(descriptor.depthStencil->format))
                    renderingInfo.stencilAttachmentFormat = dsVkFmt;
            }

            vk::GraphicsPipelineCreateInfo pipeInfo{};
            if (desc.layout->bindingModel == WGPUBindingModel::DescriptorBuffer)
                pipeInfo.flags |= vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
            pipeInfo.pNext = &renderingInfo;
            pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipeInfo.pStages = stages.data();
            pipeInfo.pVertexInputState = &vertexInputState;
            pipeInfo.pInputAssemblyState = &inputAssembly;
            pipeInfo.pViewportState = &viewportState;
            pipeInfo.pRasterizationState = &rasterState;
            pipeInfo.pMultisampleState = &multisampleState;
            pipeInfo.pDepthStencilState = desc.hasDepthStencil ? &depthStencilState : nullptr;
            pipeInfo.pColorBlendState = &colorBlendState;
            pipeInfo.pDynamicState = &dynamicState;
            pipeInfo.layout =
                vk::PipelineLayout{PeFromBackendHandle<VkPipelineLayout>(desc.layout->backendLayout)};
            pipeInfo.renderPass = nullptr;
            pipeInfo.subpass = 0;
            pipeInfo.basePipelineHandle = nullptr;
            pipeInfo.basePipelineIndex = -1;

            vk::Pipeline pipeline{};
            try
            {
                auto result = vkDev.createGraphicsPipeline(nullptr, pipeInfo);
                if (result.result != vk::Result::eSuccess)
                {
                    for (auto m : tempModules)
                        vkDev.destroyShaderModule(m);
                    return InternalError("createRenderPipeline: vkCreateGraphicsPipelines failed");
                }
                pipeline = result.value;
            }
            catch (const std::exception &e)
            {
                for (auto m : tempModules)
                    vkDev.destroyShaderModule(m);
                return InternalError(std::string("createRenderPipeline: vkCreateGraphicsPipelines threw: ") +
                                     e.what());
            }

            for (auto m : tempModules)
                vkDev.destroyShaderModule(m);

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(static_cast<VkPipeline>(pipeline));
            return result;
        }
        case PE_GRAPHICS_API_DX12:
        {
#if defined(PE_WIN32)
            auto *rhi = static_cast<pe::Dx12RhiImpl *>(desc.device->rhi ? desc.device->rhi->GetImpl() : nullptr);
            if (!rhi || !rhi->GetDevice() || !rhi->GetSharedRootSig())
                return InternalError("createRenderPipeline: DX12 RHI device/root signature is unavailable");

            std::vector<uint8_t> vertexDxil;
            std::string shaderError;
            if (!GetDx12WebGPUDxil(*desc.vertexSpirv,
                                   desc.vertexEntryPoint,
                                   "vertex",
                                   L"vs_6_6",
                                   vertexDxil,
                                   shaderError))
            {
                std::string message = "createRenderPipeline: failed to prepare vertex DX12 DXIL";
                if (!shaderError.empty())
                    message += ": " + shaderError;
                return InternalError(std::move(message));
            }

            std::vector<uint8_t> fragmentDxil;
            if (desc.hasFragment)
            {
                shaderError.clear();
                if (!GetDx12WebGPUDxil(*desc.fragmentSpirv,
                                       desc.fragmentEntryPoint,
                                       "fragment",
                                       L"ps_6_6",
                                       fragmentDxil,
                                       shaderError))
                {
                    std::string message = "createRenderPipeline: failed to prepare fragment DX12 DXIL";
                    if (!shaderError.empty())
                        message += ": " + shaderError;
                    return InternalError(std::move(message));
                }
            }

            const WGPURenderPipelineDescriptor &descriptor = *desc.descriptor;

            std::vector<std::string> semanticNames;
            std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
            size_t inputElementCount = 0;
            for (uint32_t slot = 0; slot < descriptor.vertex.bufferCount; ++slot)
                inputElementCount += descriptor.vertex.buffers[slot].attributeCount;
            semanticNames.reserve(inputElementCount);
            inputElements.reserve(inputElementCount);
            for (uint32_t slot = 0; slot < descriptor.vertex.bufferCount; ++slot)
            {
                const WGPUVertexBufferLayout &vbuf = descriptor.vertex.buffers[slot];
                if (vbuf.attributeCount == 0 && vbuf.arrayStride == 0)
                    continue;

                for (size_t a = 0; a < vbuf.attributeCount; ++a)
                {
                    const WGPUVertexAttribute &attr = vbuf.attributes[a];
                    semanticNames.push_back("LOC");

                    D3D12_INPUT_ELEMENT_DESC element{};
                    element.SemanticName = semanticNames.back().c_str();
                    element.SemanticIndex = attr.shaderLocation;
                    element.Format = VertexFormatToDxgi(attr.format);
                    element.InputSlot = slot;
                    element.AlignedByteOffset = static_cast<UINT>(attr.offset);
                    element.InputSlotClass =
                        (vbuf.stepMode == WGPUVertexStepMode_Instance)
                            ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                            : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    element.InstanceDataStepRate =
                        (vbuf.stepMode == WGPUVertexStepMode_Instance) ? 1u : 0u;
                    inputElements.push_back(element);
                }
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC gpso{};
            gpso.pRootSignature = rhi->GetSharedRootSig()->Get();
            gpso.VS = {vertexDxil.data(), vertexDxil.size()};
            if (!fragmentDxil.empty())
                gpso.PS = {fragmentDxil.data(), fragmentDxil.size()};
            gpso.InputLayout = {
                inputElements.empty() ? nullptr : inputElements.data(),
                static_cast<UINT>(inputElements.size())};
            gpso.PrimitiveTopologyType = ToDx12TopologyType(desc.primitiveTopology);

            D3D12_BLEND_DESC blendDesc{};
            blendDesc.AlphaToCoverageEnable =
                descriptor.multisample.alphaToCoverageEnabled ? TRUE : FALSE;
            blendDesc.IndependentBlendEnable =
                (desc.hasFragment && descriptor.fragment->targetCount > 1) ? TRUE : FALSE;
            const D3D12_RENDER_TARGET_BLEND_DESC defaultBlend =
                ToDx12BlendAttachment(WGPUColorTargetState{});
            for (auto &target : blendDesc.RenderTarget)
                target = defaultBlend;
            if (desc.hasFragment)
            {
                const uint32_t targetCount = std::min<uint32_t>(
                    static_cast<uint32_t>(descriptor.fragment->targetCount),
                    D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
                for (uint32_t i = 0; i < targetCount; ++i)
                    blendDesc.RenderTarget[i] =
                        ToDx12BlendAttachment(descriptor.fragment->targets[i]);
            }
            gpso.BlendState = blendDesc;
            gpso.SampleMask = descriptor.multisample.mask ? descriptor.multisample.mask : UINT_MAX;

            D3D12_RASTERIZER_DESC rasterizer{};
            rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
            rasterizer.CullMode = ToDx12CullMode(ResolveCullMode(descriptor.primitive.cullMode));
            // WebGPU's frontFace is expressed in framebuffer coordinates. The
            // negative-height viewport already maps DX12 into that coordinate
            // space, so preserve the WebGPU winding in the PSO.
            rasterizer.FrontCounterClockwise =
                (ResolveFrontFace(descriptor.primitive.frontFace) == WGPUFrontFace_CCW) ? TRUE : FALSE;
            rasterizer.DepthBias = desc.hasDepthStencil ? descriptor.depthStencil->depthBias : 0;
            rasterizer.DepthBiasClamp =
                desc.hasDepthStencil ? descriptor.depthStencil->depthBiasClamp : 0.0f;
            rasterizer.SlopeScaledDepthBias =
                desc.hasDepthStencil ? descriptor.depthStencil->depthBiasSlopeScale : 0.0f;
            rasterizer.DepthClipEnable = descriptor.primitive.unclippedDepth ? FALSE : TRUE;
            rasterizer.MultisampleEnable = desc.sampleCount > 1 ? TRUE : FALSE;
            rasterizer.AntialiasedLineEnable = FALSE;
            rasterizer.ForcedSampleCount = 0;
            rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            gpso.RasterizerState = rasterizer;

            D3D12_DEPTH_STENCIL_DESC depthStencil{};
            depthStencil.DepthEnable = FALSE;
            depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            depthStencil.StencilEnable = FALSE;
            depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            depthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            depthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            depthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            depthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            depthStencil.BackFace = depthStencil.FrontFace;

            if (desc.hasDepthStencil)
            {
                const WGPUDepthStencilState &ds = *descriptor.depthStencil;
                depthStencil.DepthEnable = HasDepthAspect(ds.format) ? TRUE : FALSE;
                depthStencil.DepthWriteMask =
                    ds.depthWriteEnabled == WGPUOptionalBool_True
                        ? D3D12_DEPTH_WRITE_MASK_ALL
                        : D3D12_DEPTH_WRITE_MASK_ZERO;
                depthStencil.DepthFunc = ToDx12Compare(
                    ds.depthCompare == WGPUCompareFunction_Undefined
                        ? WGPUCompareFunction_Always
                        : ds.depthCompare);
                depthStencil.StencilEnable = HasStencilAspect(ds.format) ? TRUE : FALSE;
                depthStencil.StencilReadMask =
                    static_cast<UINT8>(ds.stencilReadMask ? ds.stencilReadMask : 0xFFu);
                depthStencil.StencilWriteMask =
                    static_cast<UINT8>(ds.stencilWriteMask ? ds.stencilWriteMask : 0xFFu);

                auto mapFace = [](const WGPUStencilFaceState &face) -> D3D12_DEPTH_STENCILOP_DESC
                {
                    WGPUCompareFunction compare = face.compare;
                    WGPUStencilOperation fail = face.failOp;
                    WGPUStencilOperation depthFail = face.depthFailOp;
                    WGPUStencilOperation pass = face.passOp;
                    if (compare == WGPUCompareFunction_Undefined ||
                        compare == WGPUCompareFunction(0))
                        compare = WGPUCompareFunction_Always;
                    if (fail == WGPUStencilOperation(0))
                        fail = WGPUStencilOperation_Keep;
                    if (depthFail == WGPUStencilOperation(0))
                        depthFail = WGPUStencilOperation_Keep;
                    if (pass == WGPUStencilOperation(0))
                        pass = WGPUStencilOperation_Keep;

                    D3D12_DEPTH_STENCILOP_DESC out{};
                    out.StencilFailOp = ToDx12StencilOp(fail);
                    out.StencilDepthFailOp = ToDx12StencilOp(depthFail);
                    out.StencilPassOp = ToDx12StencilOp(pass);
                    out.StencilFunc = ToDx12Compare(compare);
                    return out;
                };
                depthStencil.FrontFace = mapFace(ds.stencilFront);
                depthStencil.BackFace = mapFace(ds.stencilBack);
            }
            gpso.DepthStencilState = depthStencil;

            if (desc.hasFragment)
            {
                gpso.NumRenderTargets = std::min<UINT>(
                    static_cast<UINT>(descriptor.fragment->targetCount),
                    D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
                for (UINT i = 0; i < gpso.NumRenderTargets; ++i)
                    gpso.RTVFormats[i] =
                        ToDxgiTextureFormat(descriptor.fragment->targets[i].format);
            }
            gpso.DSVFormat = desc.hasDepthStencil
                                 ? ToDxgiTextureFormat(descriptor.depthStencil->format)
                                 : DXGI_FORMAT_UNKNOWN;
            gpso.SampleDesc.Count = desc.sampleCount ? desc.sampleCount : 1;
            gpso.SampleDesc.Quality = 0;
            gpso.NodeMask = 0;
            gpso.CachedPSO = {};
            gpso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

            ID3D12PipelineState *pipeline = nullptr;
            HRESULT hr = rhi->GetDevice()->CreateGraphicsPipelineState(&gpso, IID_PPV_ARGS(&pipeline));
            if (FAILED(hr) || !pipeline)
            {
                std::string message = "createRenderPipeline: CreateGraphicsPipelineState failed";
                char code[32];
                std::snprintf(code, sizeof(code), " (0x%08X)", static_cast<unsigned>(hr));
                message += code;
                const std::string info = Dx12InfoQueueTail(rhi->GetDevice());
                if (!info.empty())
                    message += ": " + info;
                return InternalError(std::move(message));
            }

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(pipeline);
            return result;
#else
            return InternalError("createRenderPipeline: DX12 backend is Windows-only");
#endif
        }
        default:
            return BackendUnsupported("createRenderPipeline", desc.device);
        }
    }

    void DestroyWebGPUPipelineBackend(WGPUDeviceImpl *device, PeBackendHandle backendPipeline)
    {
        if (!device || !device->rhi || backendPipeline == 0)
            return;

        switch (DeviceApi(device))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::VulkanRhi::Device().destroyPipeline(
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(backendPipeline)});
            break;
        case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
            if (auto *pipeline = PeFromBackendHandle<ID3D12PipelineState *>(backendPipeline))
                pipeline->Release();
            break;
#else
            PE_ERROR("[WebGPU] DX12 backend pipeline destruction is Windows-only");
            break;
#endif
        default:
            PE_ERROR("[WebGPU] %s backend pipeline destruction is not implemented",
                     PeGraphicsApiName(DeviceApi(device)));
            break;
        }
    }
} // namespace pwgpu
