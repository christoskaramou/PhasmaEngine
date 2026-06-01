#pragma once

#include "Base/PhasmaExport.h"
#include "API/RHITypes.h"

namespace pe
{
    struct Settings
    {
    public:
        template <class T>
        static T &Get()
        {
            ValidateBaseClass<Settings, T>();
            static T value{};
            return value;
        }
    };

    class Image;

    enum class RenderMode : uint32_t
    {
        Raster = 0,    // Rasterization only
        Hybrid = 1,    // Raster opaque + RT transparent
        RayTracing = 2 // Full Ray Tracing
    };

    inline RenderMode ClampRenderModeToRayTracingSupport(RenderMode mode, bool rayTracingSupported)
    {
        switch (mode)
        {
        case RenderMode::Raster:
            return RenderMode::Raster;
        case RenderMode::Hybrid:
        case RenderMode::RayTracing:
            return rayTracingSupported ? mode : RenderMode::Raster;
        default:
            return rayTracingSupported ? RenderMode::Hybrid : RenderMode::Raster;
        }
    }

    struct LoadingInfo
    {
    public:
        void SetName(const std::string &newName)
        {
            std::lock_guard<std::mutex> lock(nameMutex);
            name = newName;
        }

        [[nodiscard]] std::string GetName() const
        {
            std::lock_guard<std::mutex> lock(nameMutex);
            return name;
        }

        std::atomic_uint32_t current{0};
        std::atomic_uint32_t total{1};

    private:
        mutable std::mutex nameMutex;
        std::string name = "Loading";
    };

    // TODO: Move settings to their classes (instead of having them all here in GlobalSettings)
    struct GlobalSettings : public Settings
    {
        bool right_handed = false;
        bool reverse_depth = true;
        bool frustum_culling = true;
        bool shadows = true;
        uint32_t shadow_map_size = 2048;
        uint32_t num_cascades = 4;
        float shadow_distance = 250.0f;
        float shadow_cascade_lambda = 0.85f;
        float shadow_normal_bias = 1.5f;
        float shadow_fade_fraction = 0.15f;
        float shadow_filter_radius = 0.75f;
        int shadow_debug_mode = 0;
        float render_scale = 0.75f;
        bool ssao = true;
        bool fxaa = false;
        bool taa = true;
        bool cas_sharpening = true;
        float cas_sharpness = 0.5f;
        bool ssr = false;
        bool tonemapping = false;
        bool dof = false;
        float dof_focus_scale = 15.0f;
        float dof_blur_range = 5.0f;
        bool bloom = false;
        float bloom_strength = 1.0f;
        float bloom_range = 1.0f;
        bool motion_blur = true;
        float motion_blur_strength = 1.0f;
        int motion_blur_samples = 16;
        bool IBL = true;
        float IBL_intensity = 1.0f;
        float lights_intensity = 1.0f;
        bool randomize_lights = false;
        static constexpr const char *DefaultSkyboxPath = "Skyboxes/golden_gate_hills/golden_gate_hills_4k.hdr";
        std::string skybox_path = DefaultSkyboxPath;
        std::array<float, 3> depth_bias{0.0f, 0.0f, -6.2f};
        float time_scale = 1.f;
        std::vector<std::string> model_list{};
        Image *current_rendering_image = nullptr;
        std::vector<Image *> rendering_images{};
        LoadingInfo loading;
        bool freeze_frustum_culling = false;
        bool draw_aabbs = false;
        bool draw_grid = true;
        bool aabbs_depth_aware = true;
        bool dynamic_rendering = true;
        bool ray_tracing_support = false;
        RenderMode render_mode = RenderMode::Hybrid;
        bool use_Disney_PBR = true;
        PePresentMode preferred_present_mode = PE_PRESENT_MODE_MAILBOX;
    };

    // Suppress per-TU instantiation — PhasmaCore.dll provides the one canonical instance.
    extern template PE_API GlobalSettings &Settings::Get<GlobalSettings>();
} // namespace pe
