#include "API/RHI.h"
#include "API/RHI_Internal.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12RhiImpl.h"
#endif
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#include "API/Vulkan/VulkanRhiImpl.h"
#include "API/Vulkan/VulkanSurfaceImpl.h"
#ifdef PE_TRACY
#include <tracy/TracyVulkan.hpp>
#endif
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Downsampler/Downsampler.h"
#include "API/Event.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RenderPass.h"
#include "API/Semaphore.h"
#include "API/Shader.h"
#include "API/StagingManager.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Base/Path.h"

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "SDL_vulkan.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

// System + Process RAM (Windows)
#if defined(PE_WIN32)
#include <psapi.h>
#include <windows.h>
#pragma comment(lib, "psapi.lib")
#endif

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace pe
{
    RHI &GetRHI()
    {
        return *RHI::Get();
    }

    RHI &RHII = GetRHI();

    vk::Format GetVulkanDepthFormat();

    static void SyncRayTracingSettingsToCaps(const RHI::Caps &caps)
    {
        auto &settings = Settings::Get<GlobalSettings>();
        settings.ray_tracing_support = caps.rayTracing;
        settings.render_mode = ClampRenderModeToRayTracingSupport(settings.render_mode, caps.rayTracing);
    }

    static inline uint32_t VkVendorID(const vk::PhysicalDevice &gpu)
    {
        return gpu.getProperties().vendorID; // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    }

    static bool HasDeviceExtension(vk::PhysicalDevice gpu, const char *name)
    {
        auto extensions = gpu.enumerateDeviceExtensionProperties();
        for (auto &extension : extensions)
            if (std::string(extension.extensionName.data()) == name)
                return true;

        return false;
    }

    static std::string ReadEnv(const char *name)
    {
#if defined(PE_WIN32)
        char *value = nullptr;
        size_t size = 0;
        if (_dupenv_s(&value, &size, name) != 0 || !value)
            return {};

        std::string result{value};
        std::free(value);
        return result;
#else
        const char *value = std::getenv(name);
        return value ? std::string{value} : std::string{};
#endif
    }

    static bool SetEnvValue(const char *name, const std::string &value)
    {
#if defined(PE_WIN32)
        return _putenv_s(name, value.c_str()) == 0;
#else
        return setenv(name, value.c_str(), 1) == 0;
#endif
    }

    static std::string TrimLowerAscii(std::string_view value)
    {
        size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
            ++begin;

        size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;

        std::string result(value.substr(begin, end - begin));
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        return result;
    }

    bool TryParseGpuAdapterPreferenceName(std::string_view value, GpuAdapterPreference &preference)
    {
        const std::string normalized = TrimLowerAscii(value);
        if (normalized.empty() || normalized == "auto" || normalized == "default")
        {
            preference = GpuAdapterPreference::Auto;
            return true;
        }
        if (normalized == "integrated" || normalized == "integrated_gpu" ||
            normalized == "integrated-gpu" || normalized == "igpu" ||
            normalized == "minimum_power" || normalized == "minimum-power")
        {
            preference = GpuAdapterPreference::IntegratedGpu;
            return true;
        }
        if (normalized == "discrete" || normalized == "discrete_gpu" ||
            normalized == "discrete-gpu" || normalized == "dgpu" ||
            normalized == "high_performance" || normalized == "high-performance")
        {
            preference = GpuAdapterPreference::DiscreteGpu;
            return true;
        }
        if (normalized == "cpu" || normalized == "software")
        {
            preference = GpuAdapterPreference::Cpu;
            return true;
        }
        return false;
    }

    const char *GpuAdapterPreferenceConfigName(GpuAdapterPreference preference)
    {
        switch (preference)
        {
        case GpuAdapterPreference::Auto:
            return "auto";
        case GpuAdapterPreference::IntegratedGpu:
            return "integrated";
        case GpuAdapterPreference::DiscreteGpu:
            return "discrete";
        case GpuAdapterPreference::Cpu:
            return "cpu";
        default:
            return "auto";
        }
    }

    const char *GpuAdapterPreferenceDisplayName(GpuAdapterPreference preference)
    {
        switch (preference)
        {
        case GpuAdapterPreference::Auto:
            return "Auto";
        case GpuAdapterPreference::IntegratedGpu:
            return "Integrated GPU";
        case GpuAdapterPreference::DiscreteGpu:
            return "Discrete GPU";
        case GpuAdapterPreference::Cpu:
            return "CPU / Software";
        default:
            return "Auto";
        }
    }

    static bool TryReadRuntimeGpuAdapterPreference(GpuAdapterPreference &preference,
                                                   std::string &warning)
    {
        Path::Init();
        const std::filesystem::path path =
            std::filesystem::path(Path::Root) / "phasma_settings.json";

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return false;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            warning = "Could not open " + path.string() + "; using automatic GPU adapter selection";
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
                      rapidjson::GetParseError_En(document.GetParseError()) +
                      "; using automatic GPU adapter selection";
            return false;
        }
        if (!document.IsObject() || !document.HasMember(kGpuAdapterPreferenceSettingsKey))
            return false;

        const rapidjson::Value &value = document[kGpuAdapterPreferenceSettingsKey];
        if (!value.IsString())
        {
            warning = path.string() + " field '" + kGpuAdapterPreferenceSettingsKey +
                      "' must be a string; using automatic GPU adapter selection";
            return false;
        }

        if (!TryParseGpuAdapterPreferenceName(value.GetString(), preference))
        {
            warning = "Invalid " + path.string() + " field '" + kGpuAdapterPreferenceSettingsKey +
                      "' value '" + value.GetString() +
                      "' (expected: auto, integrated, discrete, cpu); using automatic GPU adapter selection";
            return false;
        }
        return true;
    }

    GpuAdapterPreference ResolveGpuAdapterPreference(std::string *warning)
    {
        const std::string envValue = ReadEnv(kGpuAdapterPreferenceEnvVar);
        if (!envValue.empty())
        {
            GpuAdapterPreference preference = GpuAdapterPreference::Auto;
            if (TryParseGpuAdapterPreferenceName(envValue, preference))
                return preference;

            if (warning)
                *warning = std::string("Invalid ") + kGpuAdapterPreferenceEnvVar +
                           " value '" + envValue +
                           "' (expected: auto, integrated, discrete, cpu); using automatic GPU adapter selection";
            return GpuAdapterPreference::Auto;
        }

        GpuAdapterPreference preference = GpuAdapterPreference::Auto;
        std::string runtimeWarning;
        if (TryReadRuntimeGpuAdapterPreference(preference, runtimeWarning))
            return preference;
        if (warning && !runtimeWarning.empty())
            *warning = std::move(runtimeWarning);
        return GpuAdapterPreference::Auto;
    }

    static bool MatchesGpuAdapterPreference(GpuAdapterPreference preference, GpuAdapterType type)
    {
        switch (preference)
        {
        case GpuAdapterPreference::Auto:
            return true;
        case GpuAdapterPreference::IntegratedGpu:
            return type == GpuAdapterType::IntegratedGpu;
        case GpuAdapterPreference::DiscreteGpu:
            return type == GpuAdapterType::DiscreteGpu;
        case GpuAdapterPreference::Cpu:
            return type == GpuAdapterType::Cpu;
        default:
            return true;
        }
    }

    static bool PathListContains(std::string_view list, const std::filesystem::path &path)
    {
        const std::string needle = path.string();
        size_t begin = 0;
        while (begin <= list.size())
        {
            size_t end = list.find(
#if defined(PE_WIN32)
                ';',
#else
                ':',
#endif
                begin);
            if (end == std::string_view::npos)
                end = list.size();

            const std::string entry = std::string(list.substr(begin, end - begin));
#if defined(PE_WIN32)
            if (TrimLowerAscii(entry) == TrimLowerAscii(needle))
                return true;
#else
            if (entry == needle)
                return true;
#endif
            if (end == list.size())
                break;
            begin = end + 1;
        }
        return false;
    }

    static bool TryReadIcdLibraryPath(const std::filesystem::path &manifest,
                                      std::filesystem::path &libraryPath)
    {
        std::ifstream file(manifest, std::ios::binary);
        if (!file.is_open())
            return false;

        std::stringstream buffer;
        buffer << file.rdbuf();

        rapidjson::Document document;
        const std::string text = buffer.str();
        document.Parse(text.c_str(), text.size());
        if (document.HasParseError() ||
            !document.IsObject() ||
            !document.HasMember("ICD") ||
            !document["ICD"].IsObject() ||
            !document["ICD"].HasMember("library_path") ||
            !document["ICD"]["library_path"].IsString())
        {
            return false;
        }

        libraryPath = document["ICD"]["library_path"].GetString();
        if (libraryPath.is_relative())
            libraryPath = manifest.parent_path() / libraryPath;
        return true;
    }

    static bool IsUsableVulkanIcdManifest(const std::filesystem::path &manifest)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(manifest, ec))
            return false;

        std::filesystem::path libraryPath;
        if (!TryReadIcdLibraryPath(manifest, libraryPath))
            return false;

        return std::filesystem::is_regular_file(libraryPath.lexically_normal(), ec);
    }

    static void AddVulkanIcdCandidate(std::vector<std::filesystem::path> &candidates,
                                      const std::filesystem::path &manifest)
    {
        if (manifest.empty())
            return;

        std::filesystem::path normalized = manifest.lexically_normal();
        auto alreadyAdded = std::find_if(candidates.begin(),
                                         candidates.end(),
                                         [&normalized](const std::filesystem::path &candidate)
                                         {
#if defined(PE_WIN32)
                                             return TrimLowerAscii(candidate.string()) == TrimLowerAscii(normalized.string());
#else
                                             return candidate == normalized;
#endif
                                         });
        if (alreadyAdded == candidates.end())
            candidates.push_back(std::move(normalized));
    }

    static std::vector<std::filesystem::path> VulkanSoftwareIcdCandidates()
    {
        std::vector<std::filesystem::path> candidates;

        const std::string explicitIcd = ReadEnv(kVulkanCpuIcdEnvVar);
        if (!explicitIcd.empty())
            AddVulkanIcdCandidate(candidates, explicitIcd);

        // The engine ships its own SwiftShader build next to the executable
        // (Phasma/Core/Libs/swiftshader -> <exe>/swiftshader via CMake), so the bundled
        // manifest is the primary CPU-renderer source. The remaining entries are
        // fallbacks for developer setups that point at a Vulkan SDK or a hand-placed ICD.
        Path::Init();
        const std::filesystem::path root = std::filesystem::path(Path::Root);
        AddVulkanIcdCandidate(candidates, root / "swiftshader" / "vk_swiftshader_icd.json");
        AddVulkanIcdCandidate(candidates, root / "vk_swiftshader_icd.json");
        AddVulkanIcdCandidate(candidates, root / "Vulkan" / "vk_swiftshader_icd.json");

        for (const char *sdkEnv : {"VULKAN_SDK", "VK_SDK_PATH"})
        {
            const std::string sdkPath = ReadEnv(sdkEnv);
            if (sdkPath.empty())
                continue;
            const std::filesystem::path sdk = std::filesystem::path(sdkPath);
            AddVulkanIcdCandidate(candidates, sdk / "Bin" / "vk_swiftshader_icd.json");
            AddVulkanIcdCandidate(candidates, sdk / "Config" / "vk_swiftshader_icd.json");
        }

#if !defined(PE_WIN32)
        // Linux ships Mesa's llvmpipe (lavapipe) as the standard software ICD.
        AddVulkanIcdCandidate(candidates, "/usr/share/vulkan/icd.d/lvp_icd.x86_64.json");
        AddVulkanIcdCandidate(candidates, "/usr/local/share/vulkan/icd.d/lvp_icd.x86_64.json");
#endif

        return candidates;
    }

    static bool ConfigureVulkanSoftwareIcd(std::string &message)
    {
        const std::string driverFiles = ReadEnv("VK_DRIVER_FILES");
        const std::string legacyDriverFiles = ReadEnv("VK_ICD_FILENAMES");
        if (!driverFiles.empty() || !legacyDriverFiles.empty())
        {
            message = "Vulkan driver override already set; relying on existing VK_DRIVER_FILES/VK_ICD_FILENAMES for CPU renderer discovery";
            return true;
        }

        for (const std::filesystem::path &candidate : VulkanSoftwareIcdCandidates())
        {
            if (!IsUsableVulkanIcdManifest(candidate))
                continue;

            const std::string existingAddDrivers = ReadEnv("VK_ADD_DRIVER_FILES");
            if (PathListContains(existingAddDrivers, candidate))
            {
                message = "Vulkan CPU renderer ICD already added: " + candidate.string();
                return true;
            }

            std::string value = candidate.string();
            if (!existingAddDrivers.empty())
            {
#if defined(PE_WIN32)
                value += ';';
#else
                value += ':';
#endif
                value += existingAddDrivers;
            }

            if (!SetEnvValue("VK_ADD_DRIVER_FILES", value))
            {
                message = "Could not set VK_ADD_DRIVER_FILES for Vulkan CPU renderer ICD: " + candidate.string();
                return false;
            }

            message = "Added Vulkan CPU renderer ICD: " + candidate.string();
            return true;
        }

        message = std::string("No Vulkan CPU renderer ICD found. Place vk_swiftshader_icd.json next to the executable or set ") +
                  kVulkanCpuIcdEnvVar + " to a software ICD manifest.";
        return false;
    }

    static void ConfigureVulkanAdapterPreferenceBeforeInstance()
    {
        std::string preferenceWarning;
        const GpuAdapterPreference preference = ResolveGpuAdapterPreference(&preferenceWarning);
        if (!preferenceWarning.empty())
            PE_WARN("[Vulkan] %s", preferenceWarning.c_str());

        if (preference != GpuAdapterPreference::Cpu)
            return;

        std::string message;
        if (ConfigureVulkanSoftwareIcd(message))
            PE_INFO("[Vulkan] %s", message.c_str());
        else
            PE_WARN("[Vulkan] %s", message.c_str());
    }

    static bool IsEnvFlagEnabled(const char *name)
    {
        const std::string flag = ReadEnv(name);
        return flag == "1" || flag == "true" || flag == "TRUE" || flag == "on" || flag == "ON";
    }

    static bool IsEnvFlagDisabled(const char *name)
    {
        const std::string flag = ReadEnv(name);
        return flag == "0" || flag == "false" || flag == "FALSE" || flag == "off" || flag == "OFF";
    }

    static void CheckRequiredVulkanFeature(bool supported, const char *message, bool warnOnly)
    {
#if defined(PE_ANDROID)
        warnOnly = true;
#endif
        if (!supported && warnOnly)
            PE_WARN("[Vulkan] Required feature missing: %s", message);
        else
            PE_ERROR_IF(!supported, "%s", message);
    }

    // Features the engine consumes unconditionally (no runtime capability guard exists for
    // them), so a missing one cannot be tolerated on any platform — fail fast instead of
    // warning and then crashing in the VMA / buffer-device-address paths moments later.
    static void RequireVulkanFeature(bool supported, const char *message)
    {
        PE_ERROR_IF(!supported, "%s", message);
    }

    static uint32_t QueryLoaderApiVersion(PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr)
    {
        uint32_t version = VK_API_VERSION_1_0;
        if (!vkGetInstanceProcAddr)
            return version;

        auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        if (enumerateInstanceVersion)
        {
            const VkResult result = enumerateInstanceVersion(&version);
            if (result != VK_SUCCESS)
                version = VK_API_VERSION_1_0;
        }
        return version;
    }

    static uint32_t ClampVulkanApiVersionToEngineMax(uint32_t version)
    {
        constexpr uint32_t engineMaxMajor = 1;
        constexpr uint32_t engineMaxMinor = 4;
        const uint32_t major = VK_API_VERSION_MAJOR(version);
        const uint32_t minor = VK_API_VERSION_MINOR(version);
        if (major > engineMaxMajor || (major == engineMaxMajor && minor > engineMaxMinor))
            return VK_API_VERSION_1_4;
        return version;
    }

    static void LogVulkanApiVersion(const char *label, uint32_t version)
    {
        PE_INFO("[Vulkan] %s: %u.%u.%u (%u)",
                label,
                VK_API_VERSION_MAJOR(version),
                VK_API_VERSION_MINOR(version),
                VK_API_VERSION_PATCH(version),
                version);
    }

#if defined(PE_WIN32)
    static bool Dx12FormatSupportsTextureSample(ID3D12Device *device, DXGI_FORMAT format)
    {
        if (!device)
            return false;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = format;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                               &support,
                                               sizeof(support))))
            return false;

        constexpr D3D12_FORMAT_SUPPORT1 kRequired =
            D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        return (support.Support1 & kRequired) == kRequired;
    }

    static bool Dx12SupportsAllBcTextureSamples(ID3D12Device *device)
    {
        static constexpr DXGI_FORMAT kBcFormats[] = {
            DXGI_FORMAT_BC1_UNORM,
            DXGI_FORMAT_BC1_UNORM_SRGB,
            DXGI_FORMAT_BC2_UNORM,
            DXGI_FORMAT_BC2_UNORM_SRGB,
            DXGI_FORMAT_BC3_UNORM,
            DXGI_FORMAT_BC3_UNORM_SRGB,
            DXGI_FORMAT_BC4_UNORM,
            DXGI_FORMAT_BC4_SNORM,
            DXGI_FORMAT_BC5_UNORM,
            DXGI_FORMAT_BC5_SNORM,
            DXGI_FORMAT_BC6H_UF16,
            DXGI_FORMAT_BC6H_SF16,
            DXGI_FORMAT_BC7_UNORM,
            DXGI_FORMAT_BC7_UNORM_SRGB,
        };

        for (DXGI_FORMAT format : kBcFormats)
        {
            if (!Dx12FormatSupportsTextureSample(device, format))
                return false;
        }
        return true;
    }
#endif

    static bool GetVkPciBusId(vk::PhysicalDevice gpu, uint32_t &domain, uint32_t &bus, uint32_t &device, uint32_t &function)
    {
#if defined(VK_EXT_pci_bus_info)
        vk::PhysicalDevicePCIBusInfoPropertiesEXT pci{};
        vk::PhysicalDeviceProperties2 props2{};
        props2.pNext = &pci;
        gpu.getProperties2(&props2);
        if (pci.sType == vk::StructureType::ePhysicalDevicePciBusInfoPropertiesEXT)
        {
            domain = pci.pciDomain;
            bus = pci.pciBus;
            device = pci.pciDevice;
            function = pci.pciFunction;
            return true;
        }
#endif
        domain = bus = device = function = 0;
        return false;
    }

    // =========================================================
    // Global VRAM backends (runtime/optional) with fallbacks
    // =========================================================

    // ---------- NVIDIA: NVML (Win/Linux, runtime load) ----------
    struct NvmlAPI
    {
#if defined(PE_WIN32)
        HMODULE dll = nullptr;
        FARPROC L(const char *n) { return GetProcAddress(dll, n); }
        bool Load()
        {
            if (dll)
                return true;
            dll = LoadLibraryA("nvml.dll");
            return dll != nullptr;
        }
#elif defined(PE_LINUX)
        void *so = nullptr;
        void *L(const char *n) { return so ? dlsym(so, n) : nullptr; }
        bool Load()
        {
            if (so)
                return true;
            so = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
            return so != nullptr;
        }
#else
        void *L(const char *) { return nullptr; }
        bool Load() { return false; }
#endif
        int (*nvmlInit_v2)() = nullptr;
        int (*nvmlShutdown)() = nullptr;
        int (*nvmlDeviceGetHandleByPciBusId_v2)(const char *, void **) = nullptr;
        int (*nvmlDeviceGetMemoryInfo)(void *, void *) = nullptr;

        bool Init()
        {
            if (!Load())
                return false;
            nvmlInit_v2 = (int (*)())L("nvmlInit_v2");
            nvmlShutdown = (int (*)())L("nvmlShutdown");
            nvmlDeviceGetHandleByPciBusId_v2 = (int (*)(const char *, void **))L("nvmlDeviceGetHandleByPciBusId_v2");
            nvmlDeviceGetMemoryInfo = (int (*)(void *, void *))L("nvmlDeviceGetMemoryInfo");
            return nvmlInit_v2 && nvmlShutdown && nvmlDeviceGetHandleByPciBusId_v2 && nvmlDeviceGetMemoryInfo && nvmlInit_v2() == 0;
        }
    } g_nvml;

    static bool NvmlGlobalVramUsedByBDF(uint32_t dom, uint32_t bus, uint32_t dev, uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        if (!g_nvml.Init())
            return false;
        char pciStr[32];
        snprintf(pciStr, sizeof(pciStr), "%04x:%02x:%02x.%u", dom, bus, dev, 0u);
        void *h = nullptr;
        if (g_nvml.nvmlDeviceGetHandleByPciBusId_v2(pciStr, &h) != 0 || !h)
            return false;
        struct
        {
            unsigned long long total, free, used;
        } mem{};
        if (g_nvml.nvmlDeviceGetMemoryInfo(h, &mem) != 0)
            return false;
        total = (uint64_t)mem.total;
        used = (uint64_t)mem.used;
        return true;
    }

// ---------- AMD: Linux sysfs amdgpu ----------
#if defined(PE_LINUX)
    static bool AmdSysfsGlobalVramByBDF(uint32_t dom, uint32_t bus, uint32_t dev, uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        char bdf[32];
        snprintf(bdf, sizeof(bdf), "%04x:%02x:%02x.%u", dom, bus, dev, 0u);
        std::string base = std::string("/sys/bus/pci/devices/") + bdf + "/";
        std::ifstream fu(base + "mem_info_vram_used");
        std::ifstream ft(base + "mem_info_vram_total");
        if (!fu || !ft)
            return false;
        fu >> used;
        ft >> total; // bytes
        return (used > 0 && total > 0);
    }
#endif

    // ---------- Intel: Level Zero Sysman (Win/Linux, runtime) ----------
    struct L0API
    {
#if defined(PE_WIN32)
        HMODULE so = nullptr;
        FARPROC L(const char *n) { return GetProcAddress(so, n); }
        bool Load()
        {
            if (so)
                return true;
            so = LoadLibraryA("ze_loader.dll");
            return so != nullptr;
        }
#else
        void *so = nullptr;
        void *L(const char *n) { return so ? dlsym(so, n) : nullptr; }
        bool Load()
        {
            if (so)
                return true;
            so = dlopen("libze_loader.so.1", RTLD_LAZY);
            return so != nullptr;
        }
#endif
        // core
        int (*zeInit)(uint32_t) = nullptr;
        int (*zeDriverGet)(uint32_t *, void **) = nullptr;
        int (*zeDeviceGet)(void *, uint32_t *, void **) = nullptr;
        int (*zeDeviceGetProperties)(void *, void *) = nullptr;
        // sysman
        int (*zesInit)(uint32_t) = nullptr;
        int (*zesDeviceGet)(void *, void **) = nullptr;
        int (*zesDeviceEnumMemoryModules)(void *, uint32_t *, void **) = nullptr;
        int (*zesMemoryGetState)(void *, void *) = nullptr;

        bool Init()
        {
            if (!Load())
                return false;
            zeInit = (int (*)(uint32_t))L("zeInit");
            zeDriverGet = (int (*)(uint32_t *, void **))L("zeDriverGet");
            zeDeviceGet = (int (*)(void *, uint32_t *, void **))L("zeDeviceGet");
            zeDeviceGetProperties = (int (*)(void *, void *))L("zeDeviceGetProperties");
            zesInit = (int (*)(uint32_t))L("zesInit");
            zesDeviceGet = (int (*)(void *, void **))L("zesDeviceGet");
            zesDeviceEnumMemoryModules = (int (*)(void *, uint32_t *, void **))L("zesDeviceEnumMemoryModules");
            zesMemoryGetState = (int (*)(void *, void *))L("zesMemoryGetState");
            if (!zeInit || !zeDriverGet || !zeDeviceGet || !zeDeviceGetProperties || !zesInit || !zesDeviceGet || !zesDeviceEnumMemoryModules || !zesMemoryGetState)
                return false;
            return zeInit(0) == 0 && zesInit(0) == 0;
        }
    } g_l0;

    static bool L0GlobalVramUsed(uint64_t &used, uint64_t &total)
    {
        used = total = 0;
        if (!g_l0.Init())
            return false;

        uint32_t nDrivers = 0;
        if (g_l0.zeDriverGet(&nDrivers, nullptr) != 0 || nDrivers == 0)
            return false;
        std::vector<void *> drivers(nDrivers);
        g_l0.zeDriverGet(&nDrivers, drivers.data());

        struct ze_device_properties_t
        {
            uint32_t stype;
            const void *pNext;
            uint32_t type;
            uint32_t vendorId;
            uint32_t deviceId;
            char name[256];
            uint32_t flags;
            uint32_t coreClockRate;
            uint32_t reserved[32];
        } props{};
        props.stype = 1;
        props.pNext = nullptr;

        for (auto d : drivers)
        {
            uint32_t nDevices = 0;
            if (g_l0.zeDeviceGet(d, &nDevices, nullptr) != 0 || nDevices == 0)
                continue;
            std::vector<void *> devs(nDevices);
            g_l0.zeDeviceGet(d, &nDevices, devs.data());
            for (auto devH : devs)
            {
                g_l0.zeDeviceGetProperties(devH, &props);
                if (props.vendorId != 0x8086)
                    continue; // Intel only

                void *sysmanDev = nullptr;
                if (g_l0.zesDeviceGet(devH, &sysmanDev) != 0 || !sysmanDev)
                    continue;
                uint32_t nMods = 0;
                if (g_l0.zesDeviceEnumMemoryModules(sysmanDev, &nMods, nullptr) != 0 || nMods == 0)
                    continue;
                std::vector<void *> mods(nMods);
                g_l0.zesDeviceEnumMemoryModules(sysmanDev, &nMods, mods.data());

                struct zes_mem_state_t
                {
                    uint32_t stype;
                    const void *pNext;
                    uint32_t type;
                    uint64_t physicalSize;
                    uint64_t free;
                    uint64_t reserved[8];
                } state{};
                state.stype = 1;
                state.pNext = nullptr;

                uint64_t u = 0, tot = 0;
                for (auto m : mods)
                {
                    if (g_l0.zesMemoryGetState(m, &state) == 0)
                    {
                        tot += state.physicalSize;
                        u += (state.physicalSize - state.free);
                    }
                }
                if (tot > 0)
                {
                    used = u;
                    total = tot;
                    return true;
                }
            }
        }
        return false;
    }

// ---------- AMD Windows: ADLX (optional) ----------
#if defined(PE_WIN32) && defined(PE_USE_ADLX)
#include <ADLXHelper.h>
#include <interfaces/IADLXPerformanceMonitoring.h>
#include <interfaces/IADLXSystem.h>
    static bool AdlxGlobalVramUsedByAdapter(uint64_t &usedOut, uint64_t &totalOut)
    {
        usedOut = totalOut = 0;

        static adlx::ADLXHelper g_adlx;
        if (g_adlx.Initialize() != ADLX_OK)
            return false;

        adlx::IADLXSystem *sys = g_adlx.GetSystemServices();
        if (!sys)
            return false;

        adlx::IADLXPerformanceMonitoringServices *pmon = nullptr;
        if (sys->GetPerformanceMonitoringServices(&pmon) != ADLX_OK || !pmon)
            return false;

        adlx::IADLXGPUs *gpus = nullptr;
        if (sys->GetGPUs(&gpus) != ADLX_OK || !gpus)
        {
            pmon->Release();
            return false;
        }

        adlx_uint count = 0;
        gpus->Size(&count);
        if (count == 0)
        {
            gpus->Release();
            pmon->Release();
            return false;
        }

        // TODO: match your Vulkan GPU via PCI if multi-GPU. For now, first GPU.
        adlx::IADLXGPU *gpu = nullptr;
        gpus->At(0, &gpu);
        if (!gpu)
        {
            gpus->Release();
            pmon->Release();
            return false;
        }

        adlx::IADLXPerformanceMetrics *metrics = nullptr;
        if (pmon->GetCurrentPerformanceMetrics(gpu, &metrics) != ADLX_OK || !metrics)
        {
            gpu->Release();
            gpus->Release();
            pmon->Release();
            return false;
        }

        adlx_int vramUsedMB = 0, vramTotalMB = 0;
        metrics->VRAMUsage(&vramUsedMB);
        metrics->VRAMTotal(&vramTotalMB);

        usedOut = (uint64_t)vramUsedMB * 1024ull * 1024ull;
        totalOut = (uint64_t)vramTotalMB * 1024ull * 1024ull;

        metrics->Release();
        gpu->Release();
        gpus->Release();
        pmon->Release();
        return usedOut > 0 && totalOut > 0;
    }
#else
    static bool AdlxGlobalVramUsedByAdapter(uint64_t &usedOut, uint64_t &totalOut)
    {
        usedOut = totalOut = 0;
        return false;
    }
#endif

    void RHI::Init(SDL_Window *window, PeGraphicsApi api)
    {
        m_api = api;
        m_window = window;
        m_frameCounter = 0;
        m_textureDataPitchAlignment = 1;
        m_caps = {};
        m_gpuAdapterInfo = {};
        m_gpuFeatureSupport = {};
        m_gpuLimits = {};

        Debug::InitCaptureApi();

        if (api == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            auto *dx = new Dx12RhiImpl();
            m_impl = dx;
            if (!m_impl->Init(window))
            {
                PE_ERROR("RHI::Init: Dx12RhiImpl::Init failed");
                return;
            }
            // Surface owns m_actualExtent used by RHI::GetWidth/Height; create it
            // before any code path that queries window size goes through RHII.
            m_surface = Surface::Create(m_window);
            m_caps = dx->GetCaps();
            SyncRayTracingSettingsToCaps(m_caps);
            m_gpuName = dx->GetAdapterName();

            DXGI_ADAPTER_DESC3 adapterDesc{};
            if (dx->GetAdapter())
            {
                dx->GetAdapter()->GetDesc3(&adapterDesc);
                m_gpuAdapterInfo.vendorId = adapterDesc.VendorId;
                m_gpuAdapterInfo.deviceId = adapterDesc.DeviceId;
                if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
                    m_gpuAdapterInfo.type = GpuAdapterType::Cpu;
                else if (adapterDesc.DedicatedVideoMemory > 0)
                    m_gpuAdapterInfo.type = GpuAdapterType::DiscreteGpu;
                else
                    m_gpuAdapterInfo.type = GpuAdapterType::IntegratedGpu;
            }

            m_gpuFeatureSupport.textureCompressionBC =
                Dx12SupportsAllBcTextureSamples(dx->GetDevice());
            m_gpuFeatureSupport.drawIndirectFirstInstance = true;
            m_gpuFeatureSupport.timestampQuery = true;
            m_maxUniformBufferSize = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16u;
            m_maxStorageBufferSize = std::numeric_limits<uint32_t>::max();
            m_minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
            m_minStorageBufferOffsetAlignment = D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
            m_textureDataPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
            m_maxPushConstantsSize = m_caps.maxPushConstantsBytes;
            m_maxDrawIndirectCount = std::numeric_limits<uint32_t>::max();
            m_gpuLimits.maxTextureDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
            m_gpuLimits.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
            m_gpuLimits.maxTextureDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
            m_gpuLimits.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
            m_gpuLimits.maxBindGroups = 4;
            m_gpuLimits.maxDynamicUniformBuffersPerPipelineLayout = 8;
            m_gpuLimits.maxDynamicStorageBuffersPerPipelineLayout = 4;
            m_gpuLimits.maxSampledTexturesPerShaderStage = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
            m_gpuLimits.maxSamplersPerShaderStage = D3D12_COMMONSHADER_SAMPLER_SLOT_COUNT;
            m_gpuLimits.maxStorageBuffersPerShaderStage = D3D12_PS_CS_UAV_REGISTER_COUNT;
            m_gpuLimits.maxStorageTexturesPerShaderStage = 4;
            m_gpuLimits.maxUniformBuffersPerShaderStage = D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
            m_gpuLimits.maxUniformBufferBindingSize = m_maxUniformBufferSize;
            m_gpuLimits.maxStorageBufferBindingSize = m_maxStorageBufferSize;
            m_gpuLimits.minUniformBufferOffsetAlignment = static_cast<uint32_t>(m_minUniformBufferOffsetAlignment);
            m_gpuLimits.minStorageBufferOffsetAlignment = static_cast<uint32_t>(m_minStorageBufferOffsetAlignment);
            m_gpuLimits.maxVertexBuffers = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
            m_gpuLimits.maxBufferSize = m_maxStorageBufferSize;
            m_gpuLimits.maxVertexAttributes = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
            m_gpuLimits.maxVertexBufferArrayStride = 2048;
            m_gpuLimits.maxInterStageShaderVariables = 16;
            m_gpuLimits.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
            m_gpuLimits.maxComputeWorkgroupStorageSize = 32768;
            m_gpuLimits.maxComputeInvocationsPerWorkgroup = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
            m_gpuLimits.maxComputeWorkgroupSizeX = D3D12_CS_THREAD_GROUP_MAX_X;
            m_gpuLimits.maxComputeWorkgroupSizeY = D3D12_CS_THREAD_GROUP_MAX_Y;
            m_gpuLimits.maxComputeWorkgroupSizeZ = D3D12_CS_THREAD_GROUP_MAX_Z;
            m_gpuLimits.maxComputeWorkgroupsPerDimension = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
            m_mainQueue = Queue::Create(0, "Main_queue");
            m_stagingManager = new StagingManager();
            CreateDescriptorPool(150);

            // Swapchain creation is deferred to RHI::InitSwapchain (called from App after the
            // preferred present mode is applied), so DX12 is born with the requested present mode
            // instead of the default-FIFO surface. Mirrors the Vulkan path and removes the need
            // for a startup present-mode recreate.
            return;
#else
            PE_ERROR("RHI::Init: DX12 backend is Windows-only");
            return;
#endif
        }
        PE_ERROR_IF(api != PE_GRAPHICS_API_VULKAN, "RHI::Init: unsupported graphics api enum %u", static_cast<uint32_t>(api));
        m_impl = new VulkanRhiImpl();
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);

        ConfigureVulkanAdapterPreferenceBeforeInstance();
        CreateInstance(window);
        FindGpu();
        CreateSurface();
        CreateDevice();
        SyncRayTracingSettingsToCaps(m_caps);
        CreateAllocator();
        CreateDescriptorPool(150); // General purpose descriptor pool

        m_stagingManager = new StagingManager();

#ifdef PE_TRACY
        if (GetApi() != PE_GRAPHICS_API_DX12)
        {
            CommandBuffer *cmd = m_mainQueue->AcquireCommandBuffer();
            auto &d = VULKAN_HPP_DEFAULT_DISPATCHER;
            vk->m_tracyVkCtx = TracyVkContext(
                static_cast<VkInstance>(vk->m_instance),
                static_cast<VkPhysicalDevice>(vk->m_gpu),
                static_cast<VkDevice>(vk->m_device),
                static_cast<VkQueue>(pe::GetVulkanQueue(m_mainQueue)),
                static_cast<VkCommandBuffer>(GetVulkanCommandBuffer(cmd)),
                d.vkGetInstanceProcAddr,
                d.vkGetDeviceProcAddr);
            TracyVkContextName(vk->m_tracyVkCtx, "Main Queue", 10);
            cmd->Return();
        }
#else
        (void)vk;
#endif
    }

    void RHI::Destroy()
    {
        WaitDeviceIdle();

        if (m_api == PE_GRAPHICS_API_DX12)
        {
            for (auto &queue : m_deletionQueues)
            {
                if (queue)
                {
                    queue->Flush();
                    delete queue;
                }
            }
            m_deletionQueues.clear();

            Downsampler::Destroy();
            DescriptorLayout::ClearCache();
            Swapchain::Destroy(m_swapchain);
            Queue::Destroy(m_mainQueue);
            CommandBuffer::ClearCache();
            delete m_stagingManager;
            m_stagingManager = nullptr;
            DescriptorPool::Destroy(m_descriptorPool);

            if (m_impl)
                m_impl->Shutdown();
            Debug::DestroyCaptureApi();
            delete m_impl;
            m_impl = nullptr;
            return;
        }

        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);

#ifdef PE_TRACY
        if (vk && vk->m_tracyVkCtx)
        {
            TracyVkDestroy(vk->m_tracyVkCtx);
            vk->m_tracyVkCtx = nullptr;
        }
#endif

        for (auto &queue : m_deletionQueues)
        {
            queue->Flush();
            delete queue;
        }
        m_deletionQueues.clear();

        Downsampler::Destroy();
        DescriptorLayout::ClearCache();
        Swapchain::Destroy(m_swapchain);
        Surface::Destroy(m_surface);
        Queue::Destroy(m_mainQueue);
        CommandBuffer::ClearCache();
        delete m_stagingManager;
        DescriptorPool::Destroy(m_descriptorPool);

#if defined(PE_TRACK_RESOURCES)
        auto buffers = Buffer::GetHandles();
        auto commandBuffers = CommandBuffer::GetHandles();
        auto descriptorPools = DescriptorPool::GetHandles();
        auto descriptorLayouts = DescriptorLayout::GetHandles();
        auto descriptors = Descriptor::GetHandles();
        auto events = Event::GetHandles();
        auto framebuffers = Framebuffer::GetHandles();
        auto samplers = Sampler::GetHandles();
        auto images = Image::GetHandles();
        auto pipelines = Pipeline::GetHandles();
        auto commandPools = CommandPool::GetHandles();
        auto queues = Queue::GetHandles();
        auto renderPasses = RenderPass::GetHandles();
        auto semaphores = Semaphore::GetHandles();
        auto shaders = Shader::GetHandles();
        auto surfaces = Surface::GetHandles();
        auto swapchains = Swapchain::GetHandles();
        auto gpuTimers = GpuTimer::GetHandles();

        auto logLeaks = [](const char *name, const auto &resources)
        {
            if (!resources.empty())
            {
                PE_WARN("[RHI] Leaked %s: %zu", name, resources.size());
                for (const auto &res : resources)
                {
                    if constexpr (requires { res->ApiHandle(); })
                        PE_WARN("[RHI]   Handle: %p", (void *)detail::ToUintPtr(res->ApiHandle()));
                    else
                        PE_WARN("[RHI]   Object: %p", (void *)res);
                }
            }
        };

        logLeaks("Buffers", buffers);
        logLeaks("CommandBuffers", commandBuffers);
        logLeaks("DescriptorPools", descriptorPools);
        logLeaks("DescriptorLayouts", descriptorLayouts);
        logLeaks("Descriptors", descriptors);
        logLeaks("Events", events);
        logLeaks("Framebuffers", framebuffers);
        logLeaks("Samplers", samplers);
        logLeaks("Images", images);
        logLeaks("Pipelines", pipelines);
        logLeaks("CommandPools", commandPools);
        logLeaks("Queues", queues);
        logLeaks("RenderPasses", renderPasses);
        logLeaks("Semaphores", semaphores);
        logLeaks("Shaders", shaders);
        logLeaks("Surfaces", surfaces);
        logLeaks("Swapchains", swapchains);
        logLeaks("GpuTimers", gpuTimers);
#endif

        if (vk)
        {
            vmaDestroyAllocator(vk->m_allocator);
            if (vk->m_device)
                vk->m_device.destroy();
        }
        Debug::DestroyDebugMessenger();
        Debug::DestroyCaptureApi();
        if (vk && vk->m_instance)
            vk->m_instance.destroy();

        delete m_impl;
        m_impl = nullptr;
    }

    void RHI::CreateInstance(SDL_Window *window)
    {
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);

        // Initialize the DynamicLoader
        static vk::detail::DynamicLoader dl;
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        std::vector<const char *> instanceExtensions{};
        std::vector<const char *> instanceLayers{};

        // === Extentions ==============================
        unsigned extCount;
        PE_ERROR_IF(!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr), SDL_GetError());
        instanceExtensions.resize(extCount);
        PE_ERROR_IF(!SDL_Vulkan_GetInstanceExtensions(window, &extCount, instanceExtensions.data()), SDL_GetError());
        // =============================================

        // === Debugging (debug_utils always enabled — needed for GPU timers in profiler) ===
        if (RHII.IsInstanceExtensionValid("VK_EXT_debug_utils"))
            instanceExtensions.push_back("VK_EXT_debug_utils");
        // =============================================

        const bool vulkanValidationRequested = IsEnvFlagEnabled("PE_VULKAN_VALIDATION");
        const bool vulkanValidationSuppressed = IsEnvFlagDisabled("PE_VULKAN_VALIDATION");
        const bool enableVulkanValidation =
#if !defined(PE_RELEASE) && !defined(PE_MINSIZEREL)
            !vulkanValidationSuppressed || vulkanValidationRequested;
#else
            vulkanValidationRequested;
#endif
        bool useValidationLayerSettings = false;
        const VkBool32 setting_true = true;
        const VkLayerSettingEXT layer_settings[] = {
            {"VK_LAYER_KHRONOS_validation", "printf_verbose", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_true},
            {"VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &setting_true}};
        VkLayerSettingsCreateInfoEXT layer_settings_create_info = {
            VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            nullptr,
            static_cast<uint32_t>(sizeof(layer_settings) / sizeof(layer_settings[0])),
            layer_settings};

        // === Layers ==================================
        if (enableVulkanValidation && RHII.IsInstanceLayerValid("VK_LAYER_KHRONOS_validation"))
        {
            instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
            PE_INFO("Vulkan validation layer enabled");
        }
        else if (vulkanValidationRequested)
        {
            PE_WARN("PE_VULKAN_VALIDATION requested, but VK_LAYER_KHRONOS_validation is unavailable");
        }
        // =============================================

        if (enableVulkanValidation && RHII.IsInstanceExtensionValid("VK_EXT_layer_settings"))
        {
            instanceExtensions.push_back("VK_EXT_layer_settings");
            useValidationLayerSettings = true;
        }

        m_caps.loaderApiVersion = QueryLoaderApiVersion(vkGetInstanceProcAddr);
        m_caps.instanceApiVersion = ClampVulkanApiVersionToEngineMax(m_caps.loaderApiVersion);
        LogVulkanApiVersion("Loader max API version", m_caps.loaderApiVersion);
        LogVulkanApiVersion("Requested instance API version", m_caps.instanceApiVersion);

        vk::ApplicationInfo appInfo{};
        appInfo.sType = vk::StructureType::eApplicationInfo;
        appInfo.pApplicationName = "PhasmaEngine";
        appInfo.pEngineName = "PhasmaEngine";

        // Create Instance
        {
            vk::InstanceCreateInfo instanceCI{};
            if (useValidationLayerSettings)
                instanceCI.pNext = &layer_settings_create_info;
            instanceCI.pApplicationInfo = &appInfo;
            instanceCI.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
            instanceCI.ppEnabledExtensionNames = instanceExtensions.data();
            instanceCI.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
            instanceCI.ppEnabledLayerNames = instanceLayers.data();

            auto createInstance = [&](uint32_t apiVersion)
            {
                appInfo.apiVersion = apiVersion;
                return ::vk::createInstance(instanceCI);
            };

#if defined(PE_ANDROID)
            try
            {
                vk->m_instance = createInstance(m_caps.instanceApiVersion);
            }
            catch (const vk::SystemError &error)
            {
                const uint32_t fallbackApiVersion =
                    std::min(m_caps.loaderApiVersion ? m_caps.loaderApiVersion : VK_API_VERSION_1_0,
                             VK_API_VERSION_1_2);
                if (m_caps.instanceApiVersion <= fallbackApiVersion)
                    throw;

                PE_WARN("[Vulkan] Instance creation failed with loader API version; retrying with Vulkan 1.2: %s",
                        error.what());
                m_caps.instanceApiVersion = fallbackApiVersion;
                LogVulkanApiVersion("Requested instance API version", m_caps.instanceApiVersion);
                vk->m_instance = createInstance(m_caps.instanceApiVersion);
            }
#else
            vk->m_instance = createInstance(m_caps.instanceApiVersion);
#endif

            VULKAN_HPP_DEFAULT_DISPATCHER.init(vk->m_instance);
        }
        Debug::Init();
        Debug::CreateDebugMessenger();
    }

    void RHI::CreateSurface()
    {
        m_surface = Surface::Create(m_window);
    }

    bool RHI::UsesDozenVulkan() const
    {
#if defined(PE_WIN32)
        return false;
#else
        return m_api == PE_GRAPHICS_API_VULKAN &&
               m_gpuName.find("Microsoft Direct3D12") != std::string::npos;
#endif
    }

    struct GPUScore
    {
        vk::PhysicalDevice gpu;
        uint32_t score;
        GpuAdapterType type = GpuAdapterType::Unknown;
        std::string name;
    };

    static GpuAdapterType VulkanGpuAdapterType(vk::PhysicalDeviceType type)
    {
        switch (type)
        {
        case vk::PhysicalDeviceType::eIntegratedGpu:
            return GpuAdapterType::IntegratedGpu;
        case vk::PhysicalDeviceType::eDiscreteGpu:
            return GpuAdapterType::DiscreteGpu;
        case vk::PhysicalDeviceType::eCpu:
            return GpuAdapterType::Cpu;
        default:
            return GpuAdapterType::Unknown;
        }
    }

    void RHI::FindGpu()
    {
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);
        auto gpuList = vk->m_instance.enumeratePhysicalDevices();
        std::vector<GPUScore> gpuScores{};
        std::string preferenceWarning;
        const GpuAdapterPreference adapterPreference = ResolveGpuAdapterPreference(&preferenceWarning);
        if (!preferenceWarning.empty())
            PE_WARN("[Vulkan] %s", preferenceWarning.c_str());

        for (auto &gpu : gpuList)
        {
            auto queueFamilyProperties = gpu.getQueueFamilyProperties2();

            for (auto &qfp : queueFamilyProperties)
            {
                if (qfp.queueFamilyProperties.queueCount > 0 &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eCompute &&
                    qfp.queueFamilyProperties.queueFlags & vk::QueueFlagBits::eTransfer)
                {
                    auto properties2 = gpu.getProperties2();

                    GPUScore gpuScore{};
                    gpuScore.gpu = gpu;
                    gpuScore.type = VulkanGpuAdapterType(properties2.properties.deviceType);
                    gpuScore.name = properties2.properties.deviceName.data();
                    switch (properties2.properties.deviceType)
                    {
                    case vk::PhysicalDeviceType::eDiscreteGpu:
                        gpuScore.score = 4;
                        break;
                    case vk::PhysicalDeviceType::eIntegratedGpu:
                        gpuScore.score = 3;
                        break;
                    case vk::PhysicalDeviceType::eVirtualGpu:
                        gpuScore.score = 2;
                        break;
                    case vk::PhysicalDeviceType::eCpu:
                        gpuScore.score = 1;
                        break;
                    default:
                        continue;
                    }

                    gpuScores.push_back(gpuScore);
                    break;
                }
            }
        }

        PE_ERROR_IF(gpuScores.empty(), "No suitable GPU found!");
        std::stable_sort(gpuScores.begin(), gpuScores.end(), [](const GPUScore &a, const GPUScore &b)
                         { return a.score > b.score; });

        const GPUScore *selectedGpu = &gpuScores.front();
        if (adapterPreference != GpuAdapterPreference::Auto)
        {
            auto preferredGpu = std::find_if(gpuScores.begin(),
                                             gpuScores.end(),
                                             [adapterPreference](const GPUScore &gpuScore)
                                             {
                                                 return MatchesGpuAdapterPreference(adapterPreference, gpuScore.type);
                                             });
            if (preferredGpu != gpuScores.end())
            {
                selectedGpu = &*preferredGpu;
                PE_INFO("[Vulkan] GPU adapter preference '%s' matched: %s",
                        GpuAdapterPreferenceConfigName(adapterPreference),
                        selectedGpu->name.c_str());
            }
            else
            {
                PE_WARN("[Vulkan] GPU adapter preference '%s' unavailable; using automatic selection",
                        GpuAdapterPreferenceConfigName(adapterPreference));
            }
        }
        vk->m_gpu = selectedGpu->gpu;

        const bool descriptorBufferExtensionAvailable =
            IsDeviceExtensionValid(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

        VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties{};
        descriptorBufferProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

        ::vk::PhysicalDeviceProperties2 gpuPropertiesVK{};
        if (descriptorBufferExtensionAvailable)
        {
            gpuPropertiesVK.pNext = &descriptorBufferProperties;
        }
        vk->m_gpu.getProperties2(&gpuPropertiesVK);
        m_caps.deviceApiVersion = gpuPropertiesVK.properties.apiVersion;
        m_caps.effectiveApiVersion = std::min(m_caps.instanceApiVersion, m_caps.deviceApiVersion);

        const vk::PhysicalDeviceFeatures gpuFeatures = vk->m_gpu.getFeatures();
        VkPhysicalDeviceVulkan11Features vk11Features{};
        vk11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClipFeatures{};
        depthClipFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
        vk11Features.pNext = &vk12Features;
        vk12Features.pNext = &depthClipFeatures;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk11Features;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2(static_cast<VkPhysicalDevice>(vk->m_gpu), &features2);

        m_gpuAdapterInfo.vendorId = gpuPropertiesVK.properties.vendorID;
        m_gpuAdapterInfo.deviceId = gpuPropertiesVK.properties.deviceID;
        switch (gpuPropertiesVK.properties.deviceType)
        {
        case vk::PhysicalDeviceType::eIntegratedGpu:
            m_gpuAdapterInfo.type = GpuAdapterType::IntegratedGpu;
            break;
        case vk::PhysicalDeviceType::eDiscreteGpu:
            m_gpuAdapterInfo.type = GpuAdapterType::DiscreteGpu;
            break;
        case vk::PhysicalDeviceType::eCpu:
            m_gpuAdapterInfo.type = GpuAdapterType::Cpu;
            break;
        default:
            m_gpuAdapterInfo.type = GpuAdapterType::Unknown;
            break;
        }

        m_gpuFeatureSupport.textureCompressionBC = gpuFeatures.textureCompressionBC != 0;
        m_gpuFeatureSupport.textureCompressionETC2 = gpuFeatures.textureCompressionETC2 != 0;
        m_gpuFeatureSupport.textureCompressionASTC = gpuFeatures.textureCompressionASTC_LDR != 0;
        m_gpuFeatureSupport.drawIndirectFirstInstance = gpuFeatures.drawIndirectFirstInstance != 0;
        m_gpuFeatureSupport.timestampQuery = gpuPropertiesVK.properties.limits.timestampComputeAndGraphics != 0;
        m_gpuFeatureSupport.dualSourceBlending = gpuFeatures.dualSrcBlend != 0;
        m_gpuFeatureSupport.shaderClipDistance = gpuFeatures.shaderClipDistance != 0;
        m_gpuFeatureSupport.shaderFloat16 = vk12Features.shaderFloat16 != 0;
        m_gpuFeatureSupport.storageInputOutput16 = vk11Features.storageInputOutput16 != 0;
        m_gpuFeatureSupport.depthClipControl = depthClipFeatures.depthClipEnable != 0;
        m_gpuFeatureSupport.depthClamp = gpuFeatures.depthClamp != 0;
        m_gpuFeatureSupport.geometryShader = gpuFeatures.geometryShader != 0;

        m_gpuName = gpuPropertiesVK.properties.deviceName.data();
        PE_INFO("[Vulkan] Selected GPU: %s", m_gpuName.c_str());
        LogVulkanApiVersion("Selected GPU max API version", m_caps.deviceApiVersion);
        LogVulkanApiVersion("Effective device API version", m_caps.effectiveApiVersion);

        m_maxUniformBufferSize = gpuPropertiesVK.properties.limits.maxUniformBufferRange;
        m_maxStorageBufferSize = gpuPropertiesVK.properties.limits.maxStorageBufferRange;
        m_minUniformBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minUniformBufferOffsetAlignment;
        m_minStorageBufferOffsetAlignment = gpuPropertiesVK.properties.limits.minStorageBufferOffsetAlignment;
        m_maxPushConstantsSize = gpuPropertiesVK.properties.limits.maxPushConstantsSize;
        m_maxDrawIndirectCount = gpuPropertiesVK.properties.limits.maxDrawIndirectCount;

        const auto &limits = gpuPropertiesVK.properties.limits;
        uint32_t maxBindlessTextures = PE_MAX_DESCRIPTORS_PER_BINDING;
        if (limits.maxPerStageDescriptorSampledImages > 0)
            maxBindlessTextures = std::min(maxBindlessTextures, limits.maxPerStageDescriptorSampledImages);
        if (limits.maxPerStageResources > 32)
            maxBindlessTextures = std::min(maxBindlessTextures, limits.maxPerStageResources - 32);
        else if (limits.maxPerStageResources > 0)
            maxBindlessTextures = std::min(maxBindlessTextures, limits.maxPerStageResources);

        m_caps.maxPushConstantsBytes = m_maxPushConstantsSize;
        m_caps.maxBindlessTextures = std::max(1u, maxBindlessTextures);

        m_gpuLimits.maxTextureDimension1D = limits.maxImageDimension1D;
        m_gpuLimits.maxTextureDimension2D = limits.maxImageDimension2D;
        m_gpuLimits.maxTextureDimension3D = limits.maxImageDimension3D;
        m_gpuLimits.maxTextureArrayLayers = limits.maxImageArrayLayers;
        m_gpuLimits.maxBindGroups = limits.maxBoundDescriptorSets;
        m_gpuLimits.maxDynamicUniformBuffersPerPipelineLayout = limits.maxDescriptorSetUniformBuffersDynamic;
        m_gpuLimits.maxDynamicStorageBuffersPerPipelineLayout = limits.maxDescriptorSetStorageBuffersDynamic;
        m_gpuLimits.maxSampledTexturesPerShaderStage = limits.maxPerStageDescriptorSampledImages;
        m_gpuLimits.maxSamplersPerShaderStage = limits.maxPerStageDescriptorSamplers;
        m_gpuLimits.maxStorageBuffersPerShaderStage = limits.maxPerStageDescriptorStorageBuffers;
        m_gpuLimits.maxStorageTexturesPerShaderStage = limits.maxPerStageDescriptorStorageImages;
        m_gpuLimits.maxUniformBuffersPerShaderStage = limits.maxPerStageDescriptorUniformBuffers;
        m_gpuLimits.maxUniformBufferBindingSize = limits.maxUniformBufferRange;
        m_gpuLimits.maxStorageBufferBindingSize = limits.maxStorageBufferRange;
        m_gpuLimits.minUniformBufferOffsetAlignment = static_cast<uint32_t>(limits.minUniformBufferOffsetAlignment);
        m_gpuLimits.minStorageBufferOffsetAlignment = static_cast<uint32_t>(limits.minStorageBufferOffsetAlignment);
        m_gpuLimits.maxVertexBuffers = limits.maxVertexInputBindings;
        m_gpuLimits.maxBufferSize = limits.maxStorageBufferRange;
        m_gpuLimits.maxVertexAttributes = limits.maxVertexInputAttributes;
        m_gpuLimits.maxVertexBufferArrayStride = limits.maxVertexInputBindingStride;
        m_gpuLimits.maxInterStageShaderVariables = limits.maxVertexOutputComponents / 4;
        m_gpuLimits.maxColorAttachments = limits.maxColorAttachments;
        m_gpuLimits.maxComputeWorkgroupStorageSize = limits.maxComputeSharedMemorySize;
        m_gpuLimits.maxComputeInvocationsPerWorkgroup = limits.maxComputeWorkGroupInvocations;
        m_gpuLimits.maxComputeWorkgroupSizeX = limits.maxComputeWorkGroupSize[0];
        m_gpuLimits.maxComputeWorkgroupSizeY = limits.maxComputeWorkGroupSize[1];
        m_gpuLimits.maxComputeWorkgroupSizeZ = limits.maxComputeWorkGroupSize[2];
        m_gpuLimits.maxComputeWorkgroupsPerDimension =
            std::min(limits.maxComputeWorkGroupCount[0],
                     std::min(limits.maxComputeWorkGroupCount[1],
                              limits.maxComputeWorkGroupCount[2]));

        m_caps.descriptorBuffer = {};
        if (descriptorBufferExtensionAvailable)
        {
            auto &db = m_caps.descriptorBuffer;
            db.descriptorBufferOffsetAlignment = descriptorBufferProperties.descriptorBufferOffsetAlignment;
            db.samplerDescriptorSize = descriptorBufferProperties.samplerDescriptorSize;
            db.combinedImageSamplerDescriptorSize = descriptorBufferProperties.combinedImageSamplerDescriptorSize;
            db.sampledImageDescriptorSize = descriptorBufferProperties.sampledImageDescriptorSize;
            db.storageImageDescriptorSize = descriptorBufferProperties.storageImageDescriptorSize;
            db.uniformBufferDescriptorSize = descriptorBufferProperties.uniformBufferDescriptorSize;
            db.robustUniformBufferDescriptorSize = descriptorBufferProperties.robustUniformBufferDescriptorSize;
            db.storageBufferDescriptorSize = descriptorBufferProperties.storageBufferDescriptorSize;
            db.robustStorageBufferDescriptorSize = descriptorBufferProperties.robustStorageBufferDescriptorSize;
        }
    }

    bool RHI::IsInstanceExtensionValid(const char *name)
    {
        auto extensions = vk::enumerateInstanceExtensionProperties();
        for (auto &extension : extensions)
        {
            if (std::string(extension.extensionName.data()) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsInstanceLayerValid(const char *name)
    {
        auto layers = vk::enumerateInstanceLayerProperties();
        for (auto &layer : layers)
        {
            if (std::string(layer.layerName.data()) == name)
                return true;
        }

        return false;
    }

    bool RHI::IsDeviceExtensionValid(const char *name)
    {
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);
        PE_ERROR_IF(!vk->m_gpu, "Must find gpu before checking device extensions!");

        return HasDeviceExtension(vk->m_gpu, name);
    }

    void RHI::CreateDevice()
    {
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);
        const uint32_t gpuApiVersion = vk->m_gpu.getProperties().apiVersion;
        m_caps.deviceApiVersion = gpuApiVersion;
        if (!m_caps.instanceApiVersion)
            m_caps.instanceApiVersion = m_caps.loaderApiVersion ? m_caps.loaderApiVersion : VK_API_VERSION_1_0;
        m_caps.effectiveApiVersion = std::min(m_caps.instanceApiVersion, m_caps.deviceApiVersion);

        const bool vulkan12Available = m_caps.effectiveApiVersion >= VK_API_VERSION_1_2;
        const bool vulkan13Available = m_caps.effectiveApiVersion >= VK_API_VERSION_1_3;
        const bool vulkan14Available = m_caps.effectiveApiVersion >= VK_API_VERSION_1_4;
        const bool pushDescriptorExtensionAvailable = IsDeviceExtensionValid(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

        PE_ERROR_IF(!IsDeviceExtensionValid(VK_KHR_SWAPCHAIN_EXTENSION_NAME), "Swapchain extension not supported!");
        PE_ERROR_IF(!vulkan12Available, "PhasmaEngine requires Vulkan 1.2 or newer!");

        std::vector<const char *> deviceExtensions{};
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (pushDescriptorExtensionAvailable)
            deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        if (IsDeviceExtensionValid(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
            deviceExtensions.push_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
        }

        const bool optionalExtensionsAllowed = vulkan13Available;
        const bool rayTracingExtensionsAvailable =
            IsDeviceExtensionValid(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_SPIRV_1_4_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) &&
            IsDeviceExtensionValid(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        const bool depthClipExtAvailable =
            optionalExtensionsAllowed && IsDeviceExtensionValid(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
        const bool descriptorBufferExtAvailable =
            optionalExtensionsAllowed && IsDeviceExtensionValid(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);

        m_caps.rayTracing = false;
        m_caps.sync2 = false;
        m_caps.dynamicRendering = false;
        m_caps.copyCommands2 = vulkan13Available;
        m_caps.extendedDynamicState = false;
        m_caps.descriptorUpdateAfterBind = false;
        m_caps.maintenance5 = m_caps.effectiveApiVersion >= VK_API_VERSION_1_4;
        m_caps.meshShaders = false;
        m_caps.pushDescriptor = false;
        m_caps.indirectCount = false;
#if defined(PE_ANDROID)
        m_caps.spirvTargetVulkanVersion = VK_API_VERSION_1_2;
#else
        m_caps.spirvTargetVulkanVersion = vulkan13Available ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
        // Offline shader-bake override. Android hard-forces the SPIR-V target to 1.2 above; to
        // pre-bake that exact cache on a desktop host, run a desktop Vulkan build with
        // PHASMA_SPIRV_TARGET=1.2 so the baked blobs are keyed and compiled for the device's
        // target. Mirrors the committed PHASMA_API env override; see tools/bake_android_shaders.ps1.
        if (const std::string target = ReadEnv("PHASMA_SPIRV_TARGET"); !target.empty())
        {
            if (target == "1.2")
                m_caps.spirvTargetVulkanVersion = VK_API_VERSION_1_2;
            else if (target == "1.3")
                m_caps.spirvTargetVulkanVersion = VK_API_VERSION_1_3;
            else
                PE_WARN("[Vulkan] Ignoring PHASMA_SPIRV_TARGET='%s' (expected 1.2 or 1.3)", target.c_str());
        }
#endif

        auto queueFamilyProperties = vk->m_gpu.getQueueFamilyProperties();
        float priority = 1.f;
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++)
        {
            if (queueFamilyProperties[i].queueCount > 0 &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer)
            {
                queueCreateInfo.queueFamilyIndex = i;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &priority;
                break;
            }
        }

        // Vulkan 1.1 features
        vk::PhysicalDeviceVulkan11Features deviceFeatures11{};

        // Vulkan 1.2 features
        vk::PhysicalDeviceVulkan12Features deviceFeatures12{};
        deviceFeatures12.pNext = &deviceFeatures11;

        void *featureQueryChain = &deviceFeatures12;

        // Vulkan 1.3 features
        vk::PhysicalDeviceVulkan13Features deviceFeatures13{};
        if (vulkan13Available)
        {
            deviceFeatures13.pNext = featureQueryChain;
            featureQueryChain = &deviceFeatures13;
        }

        vk::PhysicalDeviceVulkan14Features deviceFeatures14{};
        if (vulkan14Available)
        {
            deviceFeatures14.pNext = featureQueryChain;
            featureQueryChain = &deviceFeatures14;
        }

        // Ray Tracing Features
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
        if (rayTracingExtensionsAvailable)
        {
            accelerationStructureFeatures.pNext = featureQueryChain;
            rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;
            rayQueryFeatures.pNext = &rayTracingPipelineFeatures;
            featureQueryChain = &rayQueryFeatures;
        }

        const bool descriptorBufferRequested = IsEnvFlagEnabled("PE_VULKAN_DESCRIPTOR_BUFFER");
        vk::PhysicalDeviceDepthClipEnableFeaturesEXT depthClipFeatures{};
        if (depthClipExtAvailable)
        {
            depthClipFeatures.pNext = featureQueryChain;
            featureQueryChain = &depthClipFeatures;
        }

        VkPhysicalDeviceDescriptorBufferFeaturesEXT supportedDescriptorBufferFeatures{};
        supportedDescriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
        VkPhysicalDeviceDescriptorBufferFeaturesEXT requestedDescriptorBufferFeatures{};
        requestedDescriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;

        vk::PhysicalDeviceFeatures2 deviceFeatures2{};
        if (descriptorBufferExtAvailable)
        {
            supportedDescriptorBufferFeatures.pNext = featureQueryChain;
            featureQueryChain = &supportedDescriptorBufferFeatures;
        }
        deviceFeatures2.pNext = featureQueryChain;

        vk->m_gpu.getFeatures2(&deviceFeatures2);

        if (rayTracingExtensionsAvailable &&
            accelerationStructureFeatures.accelerationStructure &&
            rayTracingPipelineFeatures.rayTracingPipeline &&
            rayQueryFeatures.rayQuery)
        {
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            m_caps.rayTracing = true;
        }

        if (depthClipExtAvailable && depthClipFeatures.depthClipEnable)
            deviceExtensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
        m_caps.pushDescriptor = pushDescriptorExtensionAvailable ||
                                (vulkan14Available && static_cast<bool>(deviceFeatures14.pushDescriptor));
        m_caps.descriptorBuffer.supported = false;
        m_caps.descriptorBuffer.robustBufferAccess = deviceFeatures2.features.robustBufferAccess != 0;
        if (descriptorBufferRequested &&
            descriptorBufferExtAvailable &&
            supportedDescriptorBufferFeatures.descriptorBuffer)
        {
            deviceExtensions.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
            m_caps.descriptorBuffer.supported = true;
            requestedDescriptorBufferFeatures.descriptorBuffer = VK_TRUE;
        }

        m_caps.sync2 = vulkan13Available && static_cast<bool>(deviceFeatures13.synchronization2);
        m_caps.dynamicRendering = vulkan13Available && static_cast<bool>(deviceFeatures13.dynamicRendering);
        m_caps.indirectCount = vulkan12Available && static_cast<bool>(deviceFeatures12.drawIndirectCount);
        m_caps.descriptorUpdateAfterBind =
            vulkan13Available &&
            static_cast<bool>(deviceFeatures12.descriptorBindingSampledImageUpdateAfterBind) &&
            static_cast<bool>(deviceFeatures12.descriptorBindingStorageImageUpdateAfterBind) &&
            static_cast<bool>(deviceFeatures12.descriptorBindingUniformBufferUpdateAfterBind) &&
            static_cast<bool>(deviceFeatures12.descriptorBindingStorageBufferUpdateAfterBind) &&
            static_cast<bool>(deviceFeatures12.descriptorBindingUniformTexelBufferUpdateAfterBind) &&
            static_cast<bool>(deviceFeatures12.descriptorBindingStorageTexelBufferUpdateAfterBind);
        Settings::Get<GlobalSettings>().dynamic_rendering &= m_caps.dynamicRendering;

        // --- Device feature contract (portable across all Vulkan-1.2 Android GPUs, not Mali-specific) ---
        // RequireVulkanFeature: consumed unconditionally with no runtime fallback, so a missing one is
        // a hard, clearly-named abort at startup (on every platform) instead of a confusing crash
        // deeper in. CheckRequiredVulkanFeature: warn-on-Android — either genuinely optional, or proven
        // unused by the shipped shaders. "Proven unused" is enforced by the offline bake
        // (tools/bake_android_shaders.ps1), which scans every baked SPIR-V blob for the matching
        // OpCapability; if a future shader starts using one, the bake fails and the feature must move
        // back up to RequireVulkanFeature here.

        // Bindless descriptor model: SPIR-V declares RuntimeDescriptorArray / ShaderNonUniform /
        // SampledImageArrayNonUniformIndexing, and VulkanDescriptorImpl unconditionally sets
        // ePartiallyBound / eVariableDescriptorCount on every bindless layout — no non-bindless path.
        RequireVulkanFeature(deviceFeatures12.descriptorBindingPartiallyBound,
                             "Partially bound descriptors are not supported on this device");
        RequireVulkanFeature(deviceFeatures12.runtimeDescriptorArray,
                             "Runtime descriptor array is not supported on this device");
        RequireVulkanFeature(deviceFeatures12.shaderSampledImageArrayNonUniformIndexing,
                             "Sampled image array non uniform indexing is not supported on this device");
        RequireVulkanFeature(deviceFeatures12.descriptorBindingVariableDescriptorCount,
                             "Variable descriptor count is not supported on this device");
        // Depth-only / stencil-only image layouts (eDepthAttachmentOptimal, eDepthReadOnlyOptimal,
        // eStencilAttachmentOptimal) are used unconditionally in VulkanImageImpl layout transitions.
        RequireVulkanFeature(deviceFeatures12.separateDepthStencilLayouts,
                             "Separate depth stencil layouts are not supported");
        // VMA + acceleration-structure / shader-binding-table device addresses (host-side).
        RequireVulkanFeature(deviceFeatures12.bufferDeviceAddress,
                             "Buffer device address is not supported");
        // GPU-culling indirect draws: ShadowPass issues drawIndexedIndirect with drawCount = mesh
        // count (>1, needs multiDrawIndirect), and each indirect command carries a per-draw
        // firstInstance (SceneBuffers, needs drawIndirectFirstInstance) used for bindless lookups.
        RequireVulkanFeature(deviceFeatures2.features.multiDrawIndirect,
                             "Multi draw indirect is not supported");
        RequireVulkanFeature(deviceFeatures2.features.drawIndirectFirstInstance,
                             "Draw indirect first instance is not supported");

        // Soft (warn-on-Android/software): the shipped shaders declare none of the matching SPIR-V
        // capabilities (verified by the bake's OpCapability scan), so requiring these would
        // needlessly reject otherwise-capable Android GPUs or CPU renderers that happen to lack them.
        const bool warnOnlyForSoftVulkanFeatures = m_gpuAdapterInfo.type == GpuAdapterType::Cpu;
        CheckRequiredVulkanFeature(deviceFeatures12.shaderStorageBufferArrayNonUniformIndexing,
                                   "Storage buffer array non uniform indexing is not supported on this device",
                                   warnOnlyForSoftVulkanFeatures);
        CheckRequiredVulkanFeature(deviceFeatures12.shaderFloat16,
                                   "Float16 is not supported on this device",
                                   warnOnlyForSoftVulkanFeatures);
        CheckRequiredVulkanFeature(deviceFeatures2.features.shaderInt16,
                                   "Int16 is not supported on this device",
                                   warnOnlyForSoftVulkanFeatures);
        CheckRequiredVulkanFeature(deviceFeatures2.features.shaderInt64,
                                   "Int64 is not supported on this device",
                                   warnOnlyForSoftVulkanFeatures);

        if (!m_caps.descriptorUpdateAfterBind)
        {
            deviceFeatures12.descriptorBindingUniformBufferUpdateAfterBind = VK_FALSE;
            deviceFeatures12.descriptorBindingSampledImageUpdateAfterBind = VK_FALSE;
            deviceFeatures12.descriptorBindingStorageImageUpdateAfterBind = VK_FALSE;
            deviceFeatures12.descriptorBindingStorageBufferUpdateAfterBind = VK_FALSE;
            deviceFeatures12.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE;
            deviceFeatures12.descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE;
        }
        deviceFeatures13.synchronization2 = m_caps.sync2 ? VK_TRUE : VK_FALSE;
        deviceFeatures13.dynamicRendering = m_caps.dynamicRendering ? VK_TRUE : VK_FALSE;
        deviceFeatures12.drawIndirectCount = m_caps.indirectCount ? VK_TRUE : VK_FALSE;
        deviceFeatures14.pushDescriptor = vulkan14Available && m_caps.pushDescriptor ? VK_TRUE : VK_FALSE;

        deviceFeatures12.pNext = &deviceFeatures11;
        void *deviceFeatureChain = &deviceFeatures12;
        if (vulkan13Available)
        {
            deviceFeatures13.pNext = deviceFeatureChain;
            deviceFeatureChain = &deviceFeatures13;
        }
        if (vulkan14Available)
        {
            deviceFeatures14.pNext = deviceFeatureChain;
            deviceFeatureChain = &deviceFeatures14;
        }
        if (m_caps.rayTracing)
        {
            accelerationStructureFeatures.pNext = deviceFeatureChain;
            rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;
            rayQueryFeatures.pNext = &rayTracingPipelineFeatures;
            deviceFeatureChain = &rayQueryFeatures;
        }
        if (depthClipExtAvailable && depthClipFeatures.depthClipEnable)
        {
            depthClipFeatures.pNext = deviceFeatureChain;
            deviceFeatureChain = &depthClipFeatures;
        }
        if (m_caps.descriptorBuffer.supported)
        {
            requestedDescriptorBufferFeatures.pNext = deviceFeatureChain;
            deviceFeatureChain = &requestedDescriptorBufferFeatures;
        }
        deviceFeatures2.pNext = deviceFeatureChain;

        PE_INFO("[Vulkan] Effective caps: sync2=%u, copyCommands2=%u, dynamicRendering=%u, extendedDynamicState=%u, descriptorUpdateAfterBind=%u, indirectCount=%u, maintenance5=%u",
                m_caps.sync2 ? 1u : 0u,
                m_caps.copyCommands2 ? 1u : 0u,
                m_caps.dynamicRendering ? 1u : 0u,
                m_caps.extendedDynamicState ? 1u : 0u,
                m_caps.descriptorUpdateAfterBind ? 1u : 0u,
                m_caps.indirectCount ? 1u : 0u,
                m_caps.maintenance5 ? 1u : 0u);

        vk::DeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.pNext = &deviceFeatures2;

        vk->m_device = vk->m_gpu.createDevice(deviceCreateInfo);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vk->m_device);

        // Debug naming
        Debug::SetObjectName(pe::GetVulkanSurface(m_surface), "RHI_surface");
        Debug::SetObjectName(vk->m_gpu, "RHI_gpu");
        Debug::SetObjectName(vk->m_device, "RHI_device");

        m_mainQueue = Queue::Create(queueCreateInfo.queueFamilyIndex, "Main_queue");
    }

    void RHI::CreateAllocator()
    {
        auto *vk = static_cast<VulkanRhiImpl *>(m_impl);
        const uint32_t apiVersion = m_caps.effectiveApiVersion ? m_caps.effectiveApiVersion : VK_API_VERSION_1_0;

        VmaAllocatorCreateInfo allocator_info = {};
        allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (m_caps.maintenance5)
            allocator_info.flags |= VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
        allocator_info.physicalDevice = vk->m_gpu;
        allocator_info.device = vk->m_device;
        allocator_info.instance = vk->m_instance;
        allocator_info.vulkanApiVersion = apiVersion;
#if defined(PE_ANDROID)
        VmaVulkanFunctions vmaFunctions{};
        vmaFunctions.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
        vmaFunctions.vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
        allocator_info.pVulkanFunctions = &vmaFunctions;
#endif

        PE_CHECK(vmaCreateAllocator(&allocator_info, &vk->m_allocator));

        m_stagingManager = new StagingManager();
    }

    void RHI::CreateSwapchain(Surface *surface)
    {
        SwapchainDesc desc{};
        if (surface)
        {
            // DX12 consumes SwapchainDesc dimensions directly; Vulkan refreshes from surface capabilities.
            const Rect2Du &extent = surface->GetActualExtent();
            desc.width = extent.width;
            desc.height = extent.height;
        }
        desc.window = m_window;
        desc.surface = surface;
        desc.presentMode = surface ? surface->GetPresentMode() : PE_PRESENT_MODE_FIFO;
        desc.backbufferCount = 2;
        desc.name = "RHI_swapchain";
        m_swapchain = Swapchain::Create(desc);
    }

    void RHI::InitSwapchain()
    {
        if (m_swapchain)
            return;

        PE_ERROR_IF(!m_surface, "RHI::InitSwapchain requires a surface");

        if (m_api == PE_GRAPHICS_API_DX12)
        {
            int sdlW = 0;
            int sdlH = 0;
            SDL_GetWindowSize(m_window, &sdlW, &sdlH);
            m_surface->SetActualExtent({0, 0, static_cast<uint32_t>(sdlW), static_cast<uint32_t>(sdlH)});
        }

        CreateSwapchain(m_surface);
        Downsampler::Init();

        m_deletionQueues.resize(GetSwapchainImageCount());
        for (auto &queue : m_deletionQueues)
            if (!queue)
                queue = new DeletionQueue();
    }

    void RHI::CreateDescriptorPool(uint32_t maxDescriptorSets)
    {
        std::vector<DescriptorPoolSize> descPoolsizes(5);
        descPoolsizes[0].type = PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descPoolsizes[0].descriptorCount = maxDescriptorSets;
        descPoolsizes[1].type = PE_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descPoolsizes[1].descriptorCount = maxDescriptorSets;
        descPoolsizes[2].type = PE_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        descPoolsizes[2].descriptorCount = maxDescriptorSets;
        descPoolsizes[3].type = PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descPoolsizes[3].descriptorCount = maxDescriptorSets;
        descPoolsizes[4].type = PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descPoolsizes[4].descriptorCount = maxDescriptorSets;
        m_descriptorPool = DescriptorPool::Create(descPoolsizes, "RHI_descriptor_pool", maxDescriptorSets);
    }

    ::PeFormat RHI::GetSwapchainFormat()
    {
        if (m_swapchain && m_swapchain->GetImageCount() > 0 && m_swapchain->GetImage(0))
            return m_swapchain->GetImage(0)->GetFormat();

        if (m_surface)
            return m_surface->GetFormat();

        return PE_FORMAT_R8G8B8A8_UNORM;
    }

    ::PeFormat RHI::GetDepthFormat()
    {
        // DX12 path: support a fixed widely-available depth format until a
        // backend-virtual depth-format query lands.
        if (GetApi() == PE_GRAPHICS_API_DX12)
            return PE_FORMAT_D32_SFLOAT;
        return FromVkFormat(GetVulkanDepthFormat());
    }

    vk::Format GetVulkanDepthFormat()
    {
        static ::vk::Format depthFormat = ::vk::Format::eUndefined;

        if (depthFormat == ::vk::Format::eUndefined)
        {
            auto *vk = static_cast<VulkanRhiImpl *>(RHII.GetImpl());
            std::vector<::vk::Format> candidates = {
                ::vk::Format::eD32Sfloat,
                ::vk::Format::eD32SfloatS8Uint,
                ::vk::Format::eD24UnormS8Uint,
            };

            for (auto &df : candidates)
            {
                auto props = vk->m_gpu.getFormatProperties(df);
                if ((props.optimalTilingFeatures & ::vk::FormatFeatureFlagBits::eDepthStencilAttachment) ==
                    ::vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                {
                    depthFormat = df;
                    break;
                }
            }

            PE_ERROR_IF(depthFormat == ::vk::Format::eUndefined, "Depth format is undefined");
        }

        return depthFormat;
    }

    void RHI::WaitDeviceIdle()
    {
        m_impl->WaitDeviceIdle();
    }

    uint32_t RHI::GetSwapchainImageCount()
    {
        return m_swapchain->GetImageCount();
    }

    SystemProcMem RHI::GetSystemAndProcessMemory()
    {
        SystemProcMem m{};

#if defined(PE_WIN32)
        // ---- System ----
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
        {
            m.sysTotal = (uint64_t)ms.ullTotalPhys;
            m.sysUsed = m.sysTotal - (uint64_t)ms.ullAvailPhys;
        }

        // ---- Process ----
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 (PROCESS_MEMORY_COUNTERS *)&pmc,
                                 sizeof(pmc)))
        {
            m.procWorkingSet = (uint64_t)pmc.WorkingSetSize;
            m.procPrivateBytes = (uint64_t)pmc.PrivateUsage; // “our” committed private memory
            m.procCommit = (uint64_t)pmc.PrivateUsage;
            m.procPeakWS = (uint64_t)pmc.PeakWorkingSetSize;
        }

#elif defined(PE_LINUX)
        // ---- System: MemTotal / MemAvailable from /proc/meminfo ----
        {
            std::ifstream mi("/proc/meminfo");
            uint64_t totalKB = 0, availKB = 0, val = 0;
            std::string key, unit;
            if (mi)
            {
                while (mi >> key >> val >> unit)
                {
                    if (key == "MemTotal:")
                        totalKB = val;
                    else if (key == "MemAvailable:")
                        availKB = val;
                    mi.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
            }
            if (totalKB && availKB)
            {
                m.sysTotal = totalKB * 1024ull;
                m.sysUsed = (totalKB - availKB) * 1024ull;
            }
            else
            {
                // Fallback: sysinfo (less accurate for caches/buffers)
                struct sysinfo si{};
                if (sysinfo(&si) == 0)
                {
                    m.sysTotal = (uint64_t)si.totalram * si.mem_unit;
                    uint64_t freeB = (uint64_t)si.freeram * si.mem_unit;
                    m.sysUsed = (m.sysTotal > freeB) ? (m.sysTotal - freeB) : 0;
                }
            }
        }

        // ---- Process: VmRSS + VmHWM from /proc/self/status ----
        uint64_t vmRSSB = 0, vmHWMB = 0;
        {
            std::ifstream st("/proc/self/status");
            if (st)
            {
                std::string k, unit;
                uint64_t vKB = 0;
                while (st >> k)
                {
                    if (k == "VmRSS:")
                    {
                        st >> vKB >> unit;
                        vmRSSB = vKB * 1024ull;
                    }
                    else if (k == "VmHWM:")
                    {
                        st >> vKB >> unit;
                        vmHWMB = vKB * 1024ull;
                    }
                    st.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
            }
        }

        // ---- Process: Private bytes from /proc/self/smaps_rollup (fallback: RSS) ----
        uint64_t privCleanB = 0, privDirtyB = 0;
        {
            std::ifstream sm("/proc/self/smaps_rollup");
            if (sm)
            {
                std::string line;
                while (std::getline(sm, line))
                {
                    if (line.rfind("Private_Clean:", 0) == 0)
                    {
                        uint64_t kb = 0;
                        std::istringstream(line.substr(14)) >> kb;
                        privCleanB = kb * 1024ull;
                    }
                    else if (line.rfind("Private_Dirty:", 0) == 0)
                    {
                        uint64_t kb = 0;
                        std::istringstream(line.substr(14)) >> kb;
                        privDirtyB = kb * 1024ull;
                    }
                }
            }
        }
        const uint64_t privTotalB = (privCleanB + privDirtyB) ? (privCleanB + privDirtyB) : vmRSSB;

        m.procWorkingSet = vmRSSB;       // Resident
        m.procPeakWS = vmHWMB;           // Peak resident
        m.procPrivateBytes = privTotalB; // “we use”
        m.procCommit = privTotalB;       // alias
#endif

        return m;
    }

    GpuMemorySnapshot RHI::GetGpuMemorySnapshot()
    {
        return m_impl->GetGpuMemorySnapshot();
    }

    GpuMemorySnapshot VulkanRhiImpl::GetGpuMemorySnapshot()
    {
        GpuMemorySnapshot snap{};

        static bool s_extMemoryBudgetChecked = false;
        static bool s_extMemoryBudgetAvailable = false;
        if (!s_extMemoryBudgetChecked)
        {
            s_extMemoryBudgetAvailable = RHII.IsDeviceExtensionValid(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
            s_extMemoryBudgetChecked = true;
        }

        if (!s_extMemoryBudgetAvailable)
            return snap;

        auto *vk = this;

        // --- Vulkan heaps/budgets baseline ---
        ::vk::PhysicalDeviceMemoryBudgetPropertiesEXT memBudget{};
        ::vk::PhysicalDeviceMemoryProperties2 props{};
        props.pNext = &memBudget;
        vk->m_gpu.getMemoryProperties2(&props);

        const auto &heaps = props.memoryProperties.memoryHeaps;
        const uint32_t heapCount = props.memoryProperties.memoryHeapCount;

        // Our VMA per-heap committed memory
        std::vector<VmaBudget> vmaBudgets(heapCount);
        if (vk->m_allocator)
            vmaGetHeapBudgets(vk->m_allocator, vmaBudgets.data());

        for (uint32_t i = 0; i < heapCount; ++i)
        {
            const bool deviceLocal =
                (heaps[i].flags & ::vk::MemoryHeapFlagBits::eDeviceLocal) == ::vk::MemoryHeapFlagBits::eDeviceLocal;

            MemoryInfo &sec = deviceLocal ? snap.vram : snap.host;

            const uint64_t heapSize = heaps[i].size;
            const uint64_t heapBudget = std::min<uint64_t>(memBudget.heapBudget[i], heapSize);
            const uint64_t heapUsed = memBudget.heapUsage[i]; // ALL Vulkan apps
            const uint64_t ourCommit = vk->m_allocator ? vmaBudgets[i].statistics.blockBytes : 0;

            sec.size += heapSize;
            sec.budget += heapBudget;
            sec.used += heapUsed; // baseline used
            sec.app += ourCommit; // our VMA-committed
            sec.heaps += 1;
        }

        // derive baseline "other" (still Vulkan-only at this point)
        auto finalize = [](MemoryInfo &mi)
        {
            mi.other = (mi.used > mi.app) ? (mi.used - mi.app) : 0;
        };
        finalize(snap.vram);
        finalize(snap.host);

        // ---- Try to override VRAM.used with cross-API global used ----
        uint32_t dom = 0, bus = 0, dev = 0, fn = 0;
        if (GetVkPciBusId(vk->m_gpu, dom, bus, dev, fn))
        {
            const uint32_t vendor = VkVendorID(vk->m_gpu);
            uint64_t gUsed = 0, gTotal = 0;
            bool ok = false;

            if (vendor == 0x10DE)
            { // NVIDIA
                ok = NvmlGlobalVramUsedByBDF(dom, bus, dev, gUsed, gTotal);
            }
#if defined(PE_LINUX)
            else if (vendor == 0x1002 || vendor == 0x1022)
            { // AMD Linux
                ok = AmdSysfsGlobalVramByBDF(dom, bus, dev, gUsed, gTotal);
            }
#endif
            else if (vendor == 0x8086)
            { // Intel Win/Linux
                ok = L0GlobalVramUsed(gUsed, gTotal);
            }
#if defined(PE_WIN32)
            else if (vendor == 0x1002 || vendor == 0x1022)
            { // AMD Windows
                ok = AdlxGlobalVramUsedByAdapter(gUsed, gTotal);
            }
#endif

            if (ok && gUsed > 0)
            {
                // Override VRAM.used globally; keep budget/size from Vulkan
                snap.vram.used = gUsed;
                snap.vram.other = (snap.vram.used > snap.vram.app) ? (snap.vram.used - snap.vram.app) : 0;
            }
        }

        return snap;
    }

    void RHI::ChangePresentMode(PePresentMode mode)
    {
        if (!m_surface)
            return;

        const PePresentMode requestedMode = mode;
        m_surface->SetPresentMode(mode);
        const PePresentMode effectiveMode = m_surface->GetPresentMode();
        Settings::Get<GlobalSettings>().preferred_present_mode = effectiveMode;

        if (effectiveMode != requestedMode)
        {
            PE_WARN("Requested present mode %s is not supported; using %s",
                    PresentModeToString(requestedMode),
                    PresentModeToString(effectiveMode));
        }

        // Set Window Title
        auto updateTitle = [&]()
        {
            std::string title = "PhasmaEngine";
            title += " - Device: " + GetGpuName();
            title += " - API: " + std::string(PeGraphicsApiName(m_api));
            title += " - Present Mode: " + std::string(PresentModeToString(effectiveMode));
#if PE_DEBUG
            title += " - Debug";
#elif PE_RELEASE
            title += " - Release";
#elif PE_MINSIZEREL
            title += " - MinSizeRel";
#elif PE_RELWITHDEBINFO
            title += " - RelWithDebInfo";
#endif

            EventSystem::DispatchEvent(EventType::SetWindowTitle, title);
        };

        if (m_swapchain && m_swapchain->GetPresentMode() == effectiveMode)
        {
            updateTitle();
            return;
        }

        WaitDeviceIdle();
        CommandBuffer::ClearFramebufferCache();
        Swapchain::Destroy(m_swapchain);
        CreateSwapchain(m_surface);
        updateTitle();
    }

    const char *RHI::PresentModeToString(PePresentMode presentMode)
    {
        switch (presentMode)
        {
        case PE_PRESENT_MODE_IMMEDIATE:
            return "Immediate";
        case PE_PRESENT_MODE_MAILBOX:
            return "Mailbox";
        case PE_PRESENT_MODE_FIFO:
            return "Fifo";
        case PE_PRESENT_MODE_FIFO_RELAXED:
            return "Fifo Relaxed";
        default:
            PE_ERROR("Unknown PresentMode");
            return "Unknown";
        }
    }

    uint32_t RHI::GetWidth() const
    {
        return m_surface->GetActualExtent().width;
    }
    uint32_t RHI::GetHeight() const
    {
        return m_surface->GetActualExtent().height;
    }
    float RHI::GetWidthf() const
    {
        return static_cast<float>(GetWidth());
    }
    float RHI::GetHeightf() const
    {
        return static_cast<float>(GetHeight());
    }
    void DeletionQueue::Push(std::function<void()> &&deletor)
    {
        std::lock_guard<std::mutex> lock(mutex);
        deletors.push_back(std::move(deletor));
    }

    void DeletionQueue::Flush()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &deletor : deletors)
            deletor();
        deletors.clear();
    }

    void RHI::AddToDeletionQueue(std::function<void()> &&deletor)
    {
        if (m_deletionQueues.empty())
        {
            deletor();
            return;
        }
        m_deletionQueues[GetFrameIndex()]->Push(std::move(deletor));
    }

    void RHI::FlushDeletionQueue(uint32_t frameIndex)
    {
        if (frameIndex < m_deletionQueues.size())
            m_deletionQueues[frameIndex]->Flush();
    }
} // namespace pe
