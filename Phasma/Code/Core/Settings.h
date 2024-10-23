#pragma once

namespace pe
{
    constexpr uint32_t SWAPCHAIN_IMAGES = 3;

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
        float render_scale = 1.0f;
        bool ssao = true;
        bool fxaa = true;
        bool ssr = true;
        bool tonemapping = false;
        bool fsr2 = false;
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
        float IBL_intensity = 0.75f;
        bool volumetric = false;
        uint32_t volumetric_steps = 32;
        float volumetric_dither_strength = 400.0f;
        float lights_intensity = 7.0f;
        float lights_range = 7.0f;
        bool fog = false;
        float fog_thickness = 0.3f;
        float fog_max_height = 3.0f;
        float fog_ground_thickness = 30.0f;
        bool randomize_lights = false;
        float sun_intensity = 7.f;
        std::array<float, 3> sun_direction{0.1f, 0.9f, 0.1f};
        int fps = 60;
        float camera_speed = 3.5f;
        std::array<float, 3> depth_bias{0.0f, 0.0f, -6.2f};
        float time_scale = 1.f;
        std::vector<std::string> file_list{};
        std::vector<std::string> shader_list{};
        Image *current_rendering_image = nullptr;
        std::vector<Image *> rendering_images{};
        std::string loading_name = "Loading";
        std::atomic_uint32_t loading_current{};
        std::atomic_uint32_t loading_total{};
        bool freeze_frustum_culling = false;
        bool draw_aabbs = false;
        bool aabbs_depth_dware = true;
        int culls_per_task = 10;
        bool dynamic_rendering = true;
    };
}
