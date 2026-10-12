#pragma once

#include "API/RHITypes.h"
#include "Base/PhasmaExport.h"

namespace pe
{
    enum class GraphicsApiSelectionSource : uint8_t
    {
        CommandLine,
        Environment,
        RuntimeConfig,
        BuiltInDefault
    };

    struct GraphicsApiSelection
    {
        PeGraphicsApi api = PE_GRAPHICS_API_VULKAN;
        GraphicsApiSelectionSource source = GraphicsApiSelectionSource::BuiltInDefault;
        std::string value;
        std::string error;
        std::string warning;

        bool Succeeded() const { return error.empty(); }
    };

    PE_API bool TryParseGraphicsApiName(std::string_view value, PeGraphicsApi &api);
    PE_API const char *GraphicsApiConfigName(PeGraphicsApi api);
    PE_API const char *GraphicsApiSelectionSourceName(GraphicsApiSelectionSource source);
    PE_API GraphicsApiSelection ResolveGraphicsApi(int argc,
                                                   char *const *argv,
                                                   const char *runtimeConfigPath = nullptr,
                                                   bool readRuntimeConfig = true);

    enum class ShaderCompiler : uint8_t; // API/Shader.h

    // --shader-compiler <dxc|slang> (or --shader-compiler=...), else "shader_compiler" in phasma_settings.json, else
    // dxc. Logs the choice; Slang falls back to dxc in builds without the runtime shader compiler.
    PE_API ShaderCompiler ResolveShaderCompiler(int argc, char *const *argv);
} // namespace pe
