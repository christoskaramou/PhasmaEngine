#pragma once

#include "API/Shader.h"
#include "API/RHI.h"
#include "API/Vulkan/VulkanShaderImpl.h"
#include "Base/FileWatcher.h"

#if defined(PE_WIN32)
#include <Windows.h>
#include <Objidl.h>
#include <Unknwn.h>
#include "dxcapi.h"
#include <wrl/client.h>
#endif

#include <cstring>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    namespace detail
    {
        struct ShaderCompileInfo
        {
            ::PeShaderStageFlags stage = 0;
            const char *entry = nullptr;
        };

        inline bool EndsWith(std::string_view value, std::string_view suffix)
        {
            return value.size() >= suffix.size() &&
                   value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        inline bool GetCompileInfo(const std::string &path, ShaderCompileInfo &info)
        {
            if (EndsWith(path, ".vert.hlsl"))
            {
                info.stage = ::PE_SHADER_STAGE_VERTEX;
                info.entry = "VSMain";
                return true;
            }
            if (EndsWith(path, ".pixel.hlsl"))
            {
                info.stage = ::PE_SHADER_STAGE_FRAGMENT;
                info.entry = "PSMain";
                return true;
            }
            if (EndsWith(path, ".comp.hlsl"))
            {
                info.stage = ::PE_SHADER_STAGE_COMPUTE;
                info.entry = "CSMain";
                return true;
            }
            return false;
        }

        inline bool EnsureShaderIsWatched(const std::string &path)
        {
            if (!std::filesystem::exists(path))
            {
                fprintf(stderr, "[Shader] Cannot open %s\n", path.c_str());
                return false;
            }

            if (!pe::FileWatcher::Get(path))
            {
                pe::FileWatcher::Add(path, [](size_t) {});
            }

            return true;
        }

#if defined(PE_WIN32)
        inline std::wstring Utf8ToWide(const std::string &text)
        {
            if (text.empty())
                return {};
            const int needed =
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            if (needed <= 0)
                return {};
            std::wstring wide(static_cast<size_t>(needed), 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), needed);
            wide.resize(static_cast<size_t>(needed - 1));
            return wide;
        }

        inline bool CompileHlslToSpirvForWebGPU(const std::string &path,
                                                const ShaderCompileInfo &info,
                                                std::vector<uint32_t> &outSpirv)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                fprintf(stderr, "[Shader] Cannot open %s\n", path.c_str());
                return false;
            }
            file.seekg(0, std::ios::end);
            const std::streampos fileSize = file.tellg();
            file.seekg(0, std::ios::beg);
            if (fileSize < 0)
                return false;

            std::string source;
            source.resize(static_cast<size_t>(fileSize));
            file.read(source.data(), static_cast<std::streamsize>(source.size()));
            if (file.gcount() != static_cast<std::streamsize>(source.size()))
                return false;

            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
            if (FAILED(hr))
                return false;

            Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
            hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
            if (FAILED(hr))
                return false;

            Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
            hr = utils->CreateDefaultIncludeHandler(&includeHandler);
            if (FAILED(hr))
                return false;

            Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
            hr = utils->CreateBlob(source.data(), static_cast<uint32_t>(source.size()),
                                   CP_UTF8, &sourceBlob);
            if (FAILED(hr))
                return false;

            const wchar_t *profile = L"cs_6_3";
            if (info.stage == PE_SHADER_STAGE_VERTEX)
                profile = L"vs_6_3";
            else if (info.stage == PE_SHADER_STAGE_FRAGMENT)
                profile = L"ps_6_3";

            const std::wstring entry = Utf8ToWide(info.entry ? info.entry : "");
            const std::wstring includeDir =
                Utf8ToWide(std::filesystem::path(path).parent_path().string());

            std::vector<LPCWSTR> args = {
                L"-spirv",
                L"-fspv-target-env=vulkan1.3",
                L"-T",
                profile,
                L"-E",
                entry.c_str(),
                L"-fspv-preserve-bindings",
                L"-fspv-preserve-interface",
                DXC_ARG_WARNINGS_ARE_ERRORS,
                DXC_ARG_PACK_MATRIX_ROW_MAJOR,
            };
            if (!includeDir.empty())
            {
                args.push_back(L"-I");
                args.push_back(includeDir.c_str());
            }

            DxcBuffer sourceBuffer{};
            sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
            sourceBuffer.Size = sourceBlob->GetBufferSize();
            sourceBuffer.Encoding = DXC_CP_UTF8;

            Microsoft::WRL::ComPtr<IDxcResult> result;
            hr = compiler->Compile(&sourceBuffer, args.data(),
                                   static_cast<uint32_t>(args.size()),
                                   includeHandler.Get(), IID_PPV_ARGS(&result));
            if (FAILED(hr) || !result)
                return false;

            HRESULT status = S_OK;
            result->GetStatus(&status);
            if (FAILED(status))
            {
                Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
                result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                if (errors && errors->GetStringLength() > 0)
                    fprintf(stderr, "%s", errors->GetStringPointer());
                return false;
            }

            Microsoft::WRL::ComPtr<IDxcBlob> object;
            result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            if (!object)
                return false;

            outSpirv.resize(object->GetBufferSize() / sizeof(uint32_t));
            std::memcpy(outSpirv.data(), object->GetBufferPointer(), object->GetBufferSize());
            return true;
        }
#endif
    } // namespace detail

    inline WGPUShaderModule MakeRuntimeShaderModule(WGPUDevice device, const char *path, const char *label)
    {
        if (!device || !path)
            return nullptr;

        std::string sourcePath = path;
        detail::ShaderCompileInfo info{};
        if (!detail::GetCompileInfo(sourcePath, info))
        {
            fprintf(stderr, "[Shader] Unsupported stage suffix for %s\n", path);
            return nullptr;
        }

        if (!detail::EnsureShaderIsWatched(sourcePath))
            return nullptr;

#if defined(PE_WIN32)
        std::vector<uint32_t> dx12Spirv;
        if (pe::RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            if (!detail::CompileHlslToSpirvForWebGPU(sourcePath, info, dx12Spirv) ||
                dx12Spirv.empty())
            {
                fprintf(stderr, "[Shader] Failed to compile %s\n", path);
                return nullptr;
            }

            WGPUShaderSourceSPIRV src{};
            src.chain.sType = WGPUSType_ShaderSourceSPIRV;
            src.code = dx12Spirv.data();
            src.codeSize = static_cast<uint32_t>(dx12Spirv.size());

            WGPUShaderModuleDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.nextInChain = &src.chain;
            return wgpuDeviceCreateShaderModule(device, &desc);
        }
#endif

        pe::Shader *shader = pe::Shader::Create({.sourcePath = sourcePath,
                                                 .entryPoint = info.entry,
                                                 .stage = info.stage});
        const uint32_t *spirv = pe::GetVulkanShaderSpirv(shader);
        const size_t spirvWords = pe::GetVulkanShaderSpirvSizeWords(shader);
        if (!spirv || spirvWords == 0)
        {
            fprintf(stderr, "[Shader] Failed to compile %s\n", path);
            pe::Shader::Destroy(shader);
            return nullptr;
        }

        WGPUShaderSourceSPIRV src{};
        src.chain.sType = WGPUSType_ShaderSourceSPIRV;
        src.code = spirv;
        src.codeSize = static_cast<uint32_t>(spirvWords);

        WGPUShaderModuleDescriptor desc{};
        desc.label = {label, WGPU_STRLEN};
        desc.nextInChain = &src.chain;
        WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &desc);
        pe::Shader::Destroy(shader);
        return mod;
    }

    inline WGPUShaderModule MakeWgslShaderModule(WGPUDevice device, const char *path, const char *label)
    {
        if (!device || !path)
            return nullptr;

        std::string sourcePath = path;
        if (!detail::EnsureShaderIsWatched(sourcePath))
            return nullptr;

        std::ifstream file(sourcePath, std::ios::binary);
        if (!file)
        {
            fprintf(stderr, "[Shader] Cannot open %s\n", path);
            return nullptr;
        }
        file.seekg(0, std::ios::end);
        const std::streampos fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        if (fileSize < 0)
        {
            fprintf(stderr, "[Shader] Cannot size %s\n", path);
            return nullptr;
        }

        std::string source;
        source.resize(static_cast<size_t>(fileSize));
        file.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (file.gcount() != static_cast<std::streamsize>(source.size()))
        {
            fprintf(stderr, "[Shader] Cannot read %s\n", path);
            return nullptr;
        }

        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = {source.data(), source.size()};

        WGPUShaderModuleDescriptor desc{};
        desc.label = {label, WGPU_STRLEN};
        desc.nextInChain = &src.chain;
        return wgpuDeviceCreateShaderModule(device, &desc);
    }
} // namespace pwgpu::test
