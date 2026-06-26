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

    enum class SceneViewAspectMode : int
    {
        Free = 0,
        Landscape16x9,
        Portrait9x16,
        Landscape19_5x9,
        Portrait9x19_5,
        Square1x1
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

    // Per-frame image-effect settings. Owned by the scene's SceneSettings (the default/global
    // profile, via the base slice below) and by each PostProcessVolume node; the render passes
    // read whichever profile the renderer resolves as active for the current frame through
    // ActivePostProcessProfile(). Field names/defaults are unchanged from the old flat
    // SceneSettings so .pescene keys and the Lua settings.* API keep working verbatim.
    struct PostProcessProfile
    {
        bool ssao = true;
        float ssao_radius = 0.5f;
        float ssao_bias = 0.025f;
        float ssao_intensity = 0.5f;
        float ssao_power = 1.0f;
        int ssao_samples = 16;
        bool fxaa = false;
        bool taa = true;
        bool cas_sharpening = true;
        float cas_sharpness = 0.5f;
        bool ssr = false;
        bool tonemapping = false;
        bool color_grading = false;
        // Keep each r/g/b triplet adjacent; the editor edits them with ImGui::DragFloat3.
        float color_grading_lift_r = 0.0f;
        float color_grading_lift_g = 0.0f;
        float color_grading_lift_b = 0.0f;
        float color_grading_gamma_r = 1.0f;
        float color_grading_gamma_g = 1.0f;
        float color_grading_gamma_b = 1.0f;
        float color_grading_gain_r = 1.0f;
        float color_grading_gain_g = 1.0f;
        float color_grading_gain_b = 1.0f;
        float color_grading_saturation = 1.0f;
        float color_grading_contrast = 1.0f;
        float color_grading_intensity = 1.0f;
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
    };

    // TODO: continue moving settings to their owning classes/components. Post-process now lives in
    // PostProcessProfile (inherited below so existing &SceneSettings::field member pointers and
    // settings.field access keep compiling); shading model (use_Disney_PBR) is next to move into
    // Material/PassInfo.
    struct SceneSettings : public Settings, public PostProcessProfile
    {
        bool right_handed = false;
        bool reverse_depth = true;
        bool frustum_culling = true;
        bool occlusion_culling = false;        // GPU Hi-Z occlusion culling (Phase 2; opt-in)
        float occlusion_culling_bias = 0.002f; // Hi-Z slack as a FRACTION of occluder depth (~0.2%);
                                               // only protects coplanar/touching surfaces from false cull
        // Discrete mesh LOD. lod_count = levels generated per mesh at load (1..4; meshopt_simplify); changing
        // it only affects newly loaded meshes. enabled/bias/distances are live (consumed by the cull shader):
        // the GPU picks a level by camera distance and swaps the draw's index range. distances are the world-
        // unit switch points LOD0->1, 1->2, 2->3; bias multiplies measured distance (>1 = switch sooner).
        bool lod_enabled = true;
        uint32_t lod_count = 4;
        float lod_bias = 1.0f;
        std::array<float, 3> lod_distances{30.0f, 90.0f, 250.0f};
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
        bool forward_plus = true;
        float lights_intensity = 1.0f;
        bool randomize_lights = false;
        // Point lights attenuate by windowed inverse-square (intensity in units of
        // luminance * distance^2) instead of the range-relative artistic falloff.
        bool physical_point_falloff = false;
        // No default skybox shipped; it is up to the user/scene to set one. An empty path takes the
        // silent solid-color fallback (no missing-file warnings); a non-empty bad path still warns.
        static constexpr const char *DefaultSkyboxPath = "";
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
        bool selection_outline = true;
        float selection_outline_color_r = 0.05f;
        float selection_outline_color_g = 0.62f;
        float selection_outline_color_b = 1.0f;
        float selection_outline_color_a = 0.95f;
        float selection_outline_thickness = 3.0f;
        float selection_outline_inner_fade = 2.0f;
        float selection_outline_outer_fade = 6.0f;
        bool dynamic_rendering = true;
        bool ray_tracing_support = false;
        RenderMode render_mode = RenderMode::Hybrid;
        bool use_Disney_PBR = true;
        PePresentMode preferred_present_mode = PE_PRESENT_MODE_FIFO;
        SceneViewAspectMode scene_view_aspect_mode = SceneViewAspectMode::Free;
    };

    // Suppress per-TU instantiation — PhasmaCore.dll provides the one canonical instance.
    extern template PE_API SceneSettings &Settings::Get<SceneSettings>();

    // The post-process profile the render passes read for the current frame. Defaults to the
    // active scene's SceneSettings (its PostProcessProfile base slice); the renderer retargets it
    // to the active PostProcessVolume during frame setup, then resets it. The active pointer is
    // pinned in PhasmaCore.dll alongside the Settings singleton so all modules agree.
    PostProcessProfile &ActivePostProcessProfile();
    void SetActivePostProcessProfile(PostProcessProfile *profile); // nullptr -> scene default
    // The "everything off" profile. Exposed so the volume resolver can blend over it as the base
    // when the Scene Settings node is inactive (a volume still enables its own effects on entry).
    const PostProcessProfile &DisabledPostProcessProfile();

    // Per-effect blend factor (0..1) for the current frame: how much of each post-process effect to
    // show. The render passes pass this into their shaders so an effect fades in/out smoothly across a
    // volume boundary instead of snapping when its on/off bool flips. 1 = full effect, 0 = none.
    // Computed by the volume resolver as lerp(baseEnabled, volumeEnabled, volumeWeight); defaults to 1
    // (no volume blend in progress -> running passes show their effect at full strength).
    struct PostProcessBlend
    {
        float ssao = 1.0f;
        float fxaa = 1.0f;
        float taa = 1.0f;
        float cas_sharpening = 1.0f;
        float ssr = 1.0f;
        float tonemapping = 1.0f;
        float color_grading = 1.0f;
        float dof = 1.0f;
        float bloom = 1.0f;
        float motion_blur = 1.0f;
        float IBL = 1.0f;
    };
    PostProcessBlend &ActivePostProcessBlend();
    void SetActivePostProcessBlend(const PostProcessBlend &blend);

    // Master switch driven by the Scene Settings node. When inactive (node disabled / deleted /
    // absent) ActivePostProcessProfile() returns an all-off profile and the renderer also drops
    // shadows, so the scene's look (post-process + shadows) turns off. Performance/debug toggles
    // (culling, Forward+, grid, AABBs) are independent and stay on. Resolved each frame.
    bool SceneSettingsActive();
    void SetSceneSettingsActive(bool active);
} // namespace pe
