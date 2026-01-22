#pragma once

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

    // TODO: Move settings to their classes (instead of having them all here in GlobalSettings)
    struct GlobalSettings : public Settings
    {
        bool right_handed = false;
        bool reverse_depth = true;
        bool frustum_culling = true;
        bool shadows = true;
        uint32_t shadow_map_size = 2048;
        uint32_t num_cascades = 4;
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
        float IBL_intensity = 0.4f;
        float lights_intensity = 7.0f;
        float lights_range = 7.0f;
        bool randomize_lights = false;
        bool day = true;
        float sun_intensity = 7.f;
        std::array<float, 3> sun_direction{0.1f, 0.9f, 0.1f};
        float camera_speed = 3.5f;
        std::array<float, 3> depth_bias{0.0f, 0.0f, -6.2f};
        float time_scale = 1.f;
        std::vector<std::string> model_list{};
        Image *current_rendering_image = nullptr;
        std::vector<Image *> rendering_images{};
        std::string loading_name = "Loading";
        std::atomic_uint32_t loading_current{0};
        std::atomic_uint32_t loading_total{1};
        bool freeze_frustum_culling = false;
        bool draw_aabbs = false;
        bool aabbs_depth_aware = true;
        bool dynamic_rendering = true;
        bool ray_tracing_support = false;
        bool use_ray_tracing = false;
        vk::PresentModeKHR preferred_present_mode = vk::PresentModeKHR::eMailbox;
    };
} // namespace pe
