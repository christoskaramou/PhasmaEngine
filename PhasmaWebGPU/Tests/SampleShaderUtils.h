#pragma once

#include "API/Shader.h"
#include "Base/FileWatcher.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <webgpu/webgpu.h>

namespace pwgpu::test
{
    namespace detail
    {
        struct ShaderCompileInfo
        {
            vk::ShaderStageFlags stage{};
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
                info.stage = vk::ShaderStageFlagBits::eVertex;
                info.entry = "VSMain";
                return true;
            }
            if (EndsWith(path, ".pixel.hlsl"))
            {
                info.stage = vk::ShaderStageFlagBits::eFragment;
                info.entry = "PSMain";
                return true;
            }
            if (EndsWith(path, ".comp.hlsl"))
            {
                info.stage = vk::ShaderStageFlagBits::eCompute;
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

        pe::Shader shader(sourcePath, info.stage, info.entry, {}, pe::ShaderCodeType::HLSL);
        if (!shader.GetSpriv() || shader.Size() == 0)
        {
            fprintf(stderr, "[Shader] Failed to compile %s\n", path);
            return nullptr;
        }

        WGPUShaderSourceSPIRV src{};
        src.chain.sType = WGPUSType_ShaderSourceSPIRV;
        src.code = shader.GetSpriv();
        src.codeSize = static_cast<uint32_t>(shader.Size());

        WGPUShaderModuleDescriptor desc{};
        desc.label = {label, WGPU_STRLEN};
        desc.nextInChain = &src.chain;
        return wgpuDeviceCreateShaderModule(device, &desc);
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
