#include "API/GraphicsApiSelection.h"

#include "Base/Log.h"
#include "Base/Path.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pe
{
    namespace
    {
        constexpr const char *kApiOption = "--api";
        constexpr const char *kApiOptionPrefix = "--api=";
        constexpr const char *kEnvVarName = "PHASMA_API";
        constexpr const char *kRuntimeConfigFileName = "phasma_settings.json";
        constexpr const char *kRuntimeConfigPrimaryKey = "graphics_api";
        constexpr const char *kRuntimeConfigLegacyKey = "api";

        struct ApiCandidate
        {
            std::string value;
            std::string label;
            bool present = false;
        };

        bool StartsWith(std::string_view value, std::string_view prefix)
        {
            return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
        }

        bool IsSupportedOnCurrentPlatform(PeGraphicsApi api)
        {
            switch (api)
            {
            case PE_GRAPHICS_API_VULKAN:
                return true;
            case PE_GRAPHICS_API_DX12:
#if defined(PE_WIN32)
                return true;
#else
                return false;
#endif
            default:
                return false;
            }
        }

        std::string UnsupportedReason(PeGraphicsApi api)
        {
            switch (api)
            {
            case PE_GRAPHICS_API_DX12:
                return "DX12 backend is Windows-only";
            default:
                return "unsupported graphics API";
            }
        }

        std::string ExpectedApiList()
        {
            return "vulkan, dx12";
        }

        std::string InvalidApiMessage(const std::string &label, const std::string &value)
        {
            return "Invalid " + label + " value '" + value + "' (expected: " + ExpectedApiList() + ")";
        }

        bool ResolveHardCandidate(const ApiCandidate &candidate, PeGraphicsApi &api, std::string &error)
        {
            if (!TryParseGraphicsApiName(candidate.value, api))
            {
                error = InvalidApiMessage(candidate.label, candidate.value);
                return false;
            }

            if (!IsSupportedOnCurrentPlatform(api))
            {
                error = "Unsupported " + candidate.label + " value '" + candidate.value + "': " +
                        UnsupportedReason(api);
                return false;
            }

            return true;
        }

        bool TryFindCommandLineApi(int argc, char *const *argv, ApiCandidate &candidate, std::string &error)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (!argv || !argv[i])
                    continue;

                const std::string_view argument(argv[i]);
                if (argument == kApiOption)
                {
                    if (i + 1 >= argc || !argv[i + 1])
                    {
                        error = "Missing --api value (expected: " + ExpectedApiList() + ")";
                        return false;
                    }

                    candidate.value = argv[++i];
                    candidate.label = kApiOption;
                    candidate.present = true;
                }
                else if (StartsWith(argument, kApiOptionPrefix))
                {
                    candidate.value = std::string(argument.substr(std::char_traits<char>::length(kApiOptionPrefix)));
                    candidate.label = kApiOption;
                    candidate.present = true;
                }
            }

            return true;
        }

        std::filesystem::path DefaultRuntimeConfigPath()
        {
            Path::Init();
            return std::filesystem::path(Path::Root) / kRuntimeConfigFileName;
        }

        bool TryReadRuntimeConfigApi(const std::filesystem::path &path, ApiCandidate &candidate, std::string &warning)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return false;

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                warning = "Could not open " + path.string() + "; falling back to vulkan";
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            const std::string text = buffer.str();

            rapidjson::Document document;
            document.Parse(text.c_str(), text.size());
            if (document.HasParseError())
            {
                warning = "Could not parse " + path.string() + ": " +
                          rapidjson::GetParseError_En(document.GetParseError()) + "; falling back to vulkan";
                return false;
            }

            if (!document.IsObject())
            {
                warning = path.string() + " must contain a JSON object; falling back to vulkan";
                return false;
            }

            const char *key = nullptr;
            if (document.HasMember(kRuntimeConfigPrimaryKey))
                key = kRuntimeConfigPrimaryKey;
            else if (document.HasMember(kRuntimeConfigLegacyKey))
                key = kRuntimeConfigLegacyKey;
            else
                return false;

            const rapidjson::Value &value = document[key];
            if (!value.IsString())
            {
                warning = path.string() + " field '" + key + "' must be a string; falling back to vulkan";
                return false;
            }

            candidate.value = value.GetString();
            candidate.label = path.string() + " field '" + key + "'";
            candidate.present = true;
            return true;
        }

        bool TryReadEnvironmentApi(ApiCandidate &candidate)
        {
#if defined(PE_WIN32)
            char *envValue = nullptr;
            size_t envValueSize = 0;
            if (::_dupenv_s(&envValue, &envValueSize, kEnvVarName) != 0 || !envValue)
                return false;

            candidate.value = envValue;
            std::free(envValue);
#else
            const char *envValue = std::getenv(kEnvVarName);
            if (!envValue)
                return false;

            candidate.value = envValue;
#endif
            candidate.label = kEnvVarName;
            candidate.present = true;
            return true;
        }

        void StoreWarning(GraphicsApiSelection &selection, std::string warning)
        {
            if (warning.empty())
                return;

            selection.warning = std::move(warning);
            PE_WARN("%s", selection.warning.c_str());
        }
    } // namespace

    bool TryParseGraphicsApiName(std::string_view value, PeGraphicsApi &api)
    {
        if (value == "vulkan")
        {
            api = PE_GRAPHICS_API_VULKAN;
            return true;
        }

        if (value == "dx12")
        {
            api = PE_GRAPHICS_API_DX12;
            return true;
        }

        return false;
    }

    const char *GraphicsApiConfigName(PeGraphicsApi api)
    {
        switch (api)
        {
        case PE_GRAPHICS_API_VULKAN:
            return "vulkan";
        case PE_GRAPHICS_API_DX12:
            return "dx12";
        default:
            return "unknown";
        }
    }

    const char *GraphicsApiSelectionSourceName(GraphicsApiSelectionSource source)
    {
        switch (source)
        {
        case GraphicsApiSelectionSource::CommandLine:
            return "command line";
        case GraphicsApiSelectionSource::Environment:
            return "PHASMA_API";
        case GraphicsApiSelectionSource::RuntimeConfig:
            return "runtime config";
        case GraphicsApiSelectionSource::BuiltInDefault:
            return "built-in default";
        default:
            return "unknown";
        }
    }

    GraphicsApiSelection ResolveGraphicsApi(int argc,
                                            char *const *argv,
                                            const char *runtimeConfigPath,
                                            bool readRuntimeConfig)
    {
        GraphicsApiSelection selection{};

        ApiCandidate cli;
        if (!TryFindCommandLineApi(argc, argv, cli, selection.error))
            return selection;

        if (cli.present)
        {
            if (!ResolveHardCandidate(cli, selection.api, selection.error))
                return selection;

            selection.source = GraphicsApiSelectionSource::CommandLine;
            selection.value = cli.value;
            return selection;
        }

        ApiCandidate env;
        if (TryReadEnvironmentApi(env))
        {
            if (!ResolveHardCandidate(env, selection.api, selection.error))
                return selection;

            selection.source = GraphicsApiSelectionSource::Environment;
            selection.value = env.value;
            return selection;
        }

        if (readRuntimeConfig)
        {
            ApiCandidate config;
            std::string warning;
            const std::filesystem::path configPath =
                runtimeConfigPath && *runtimeConfigPath ? std::filesystem::path(runtimeConfigPath) : DefaultRuntimeConfigPath();
            if (TryReadRuntimeConfigApi(configPath, config, warning))
            {
                PeGraphicsApi configApi = PE_GRAPHICS_API_VULKAN;
                if (!TryParseGraphicsApiName(config.value, configApi))
                {
                    StoreWarning(selection, InvalidApiMessage(config.label, config.value) + "; falling back to vulkan");
                }
                else if (!IsSupportedOnCurrentPlatform(configApi))
                {
                    StoreWarning(selection,
                                 "Unsupported " + config.label + " value '" + config.value + "': " +
                                     UnsupportedReason(configApi) + "; falling back to vulkan");
                }
                else
                {
                    selection.api = configApi;
                    selection.source = GraphicsApiSelectionSource::RuntimeConfig;
                    selection.value = config.value;
                    return selection;
                }
            }
            else
            {
                StoreWarning(selection, std::move(warning));
            }
        }

        selection.api = PE_GRAPHICS_API_VULKAN;
        selection.source = GraphicsApiSelectionSource::BuiltInDefault;
        selection.value = GraphicsApiConfigName(selection.api);
        return selection;
    }
} // namespace pe
