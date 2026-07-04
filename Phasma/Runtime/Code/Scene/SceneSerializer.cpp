#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneRuntimeHooks.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#ifdef PE_PHYSICS2D
#include "Systems/Physics2DSystem.h"
#endif
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pe
{
    namespace
    {
        constexpr float kInfiniteAttenuationDistanceSentinel = -1.0f;

        void EncodeMaterialFactorsForPersistence(RenderType renderType, mat4 &f0, mat4 &f1)
        {
            (void)f0;

            if (renderType == RenderType::Transmission && std::isinf(f1[0][1]))
                f1[0][1] = kInfiniteAttenuationDistanceSentinel;
        }

        void DecodeMaterialFactorsFromPersistence(RenderType renderType, mat4 &f0, mat4 &f1)
        {
            (void)f0;

            // Older scene files serialized +inf attenuation distance as 0.0 via SafeFloat(),
            // which makes transmission materials attenuate to black when restored.
            if (renderType == RenderType::Transmission && f1[0][1] <= 0.0f)
                f1[0][1] = std::numeric_limits<float>::infinity();
        }

        ModelAsset *CreatePrimitiveModelFromSource(const std::string &ptype, const vec4 &params, uint32_t paramCount)
        {
            auto intParam = [&](uint32_t index, int fallback, int minValue, int maxValue) -> int
            {
                if (paramCount <= index)
                    return fallback;
                if (!std::isfinite(params[index]))
                    return fallback;
                return std::clamp(static_cast<int>(std::round(params[index])), minValue, maxValue);
            };

            if (ptype == "cube")
                return Primitives::CreateCube(paramCount >= 1 ? params.x : 1.0f);
            if (ptype == "sphere")
                return Primitives::CreateSphere(paramCount >= 1 ? params.x : 1.0f);
            if (ptype == "uv_sphere")
                return Primitives::CreateUvSphere(paramCount >= 1 ? params.x : 1.0f,
                                                  intParam(1, 32, 3, 512),
                                                  intParam(2, 32, 2, 512));
            if (ptype == "ico_sphere")
                return Primitives::CreateIcoSphere(paramCount >= 1 ? params.x : 1.0f, intParam(1, 2, 0, 6));
            if (ptype == "plane")
                return Primitives::CreatePlane(paramCount >= 1 ? params.x : 10.0f, paramCount >= 2 ? params.y : 10.0f);
            if (ptype == "grid")
                return Primitives::CreateGrid(paramCount >= 1 ? params.x : 10.0f,
                                              paramCount >= 2 ? params.y : 10.0f,
                                              intParam(2, 10, 1, 512));
            if (ptype == "cylinder")
                return Primitives::CreateCylinder(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 2.0f);
            if (ptype == "cone")
                return Primitives::CreateCone(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 2.0f);
            if (ptype == "pyramid")
                return Primitives::CreatePyramid(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 1.0f);
            if (ptype == "quad")
                return Primitives::CreateQuad(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 1.0f);
            if (ptype == "circle")
                return Primitives::CreateCircle(paramCount >= 1 ? params.x : 1.0f, intParam(1, 64, 3, 512));
            if (ptype == "torus")
                return Primitives::CreateTorus(paramCount >= 1 ? params.x : 1.0f,
                                               paramCount >= 2 ? params.y : 0.25f,
                                               intParam(2, 64, 3, 512),
                                               intParam(3, 16, 3, 256));
            if (ptype == "skinned_strip_2d")
                return Primitives::CreateSkinnedStrip2D(paramCount >= 1 ? params.x : 4.0f,
                                                        paramCount >= 2 ? params.y : 1.0f,
                                                        intParam(2, 32, 1, 512),
                                                        intParam(3, 24, 2, 64));
            return nullptr;
        }

        vec4 ReadPrimitiveParams(const rapidjson::Value &srcVal, uint32_t &outCount)
        {
            vec4 params(0.f);
            outCount = 0;
            if (!srcVal.HasMember("primitive_params") || !srcVal["primitive_params"].IsArray())
                return params;

            const auto &arr = srcVal["primitive_params"];
            outCount = std::min<uint32_t>(static_cast<uint32_t>(arr.Size()), 4u);
            for (uint32_t i = 0; i < outCount; ++i)
                params[i] = arr[i].GetFloat();
            return params;
        }

        void WritePrimitiveParams(rapidjson::Value &dstObj, const vec4 &params, uint32_t count, rapidjson::Document::AllocatorType &allocator)
        {
            if (count == 0)
                return;

            rapidjson::Value arr(rapidjson::kArrayType);
            for (uint32_t i = 0; i < count && i < 4; ++i)
                arr.PushBack(params[i], allocator);
            dstObj.AddMember("primitive_params", arr.Move(), allocator);
        }

        void AddStringVectorMember(rapidjson::Value &settings,
                                   const char *name,
                                   const std::vector<std::string> &values,
                                   rapidjson::Document::AllocatorType &allocator)
        {
            rapidjson::Value array(rapidjson::kArrayType);
            for (const std::string &value : values)
                array.PushBack(rapidjson::Value(value.c_str(), allocator), allocator);
            settings.AddMember(rapidjson::Value(name, allocator), array.Move(), allocator);
        }

        void ReadStringVectorMember(const rapidjson::Value &settings, const char *name, std::vector<std::string> &out)
        {
            if (!settings.HasMember(name) || !settings[name].IsArray())
                return;

            out.clear();
            const auto &array = settings[name];
            out.reserve(array.Size());
            for (const auto &value : array.GetArray())
                if (value.IsString())
                    out.emplace_back(value.GetString());
        }

        // Post-process subset, reused by SceneSettings (the scene default profile) and by each
        // PostProcessVolume node. Keys match the flat SceneSettings keys so old scenes still load.
        void AddPostProcessProfileMembers(rapidjson::Value &v, rapidjson::Document::AllocatorType &alloc,
                                          const PostProcessProfile &p)
        {
            v.AddMember("ssao", p.ssao, alloc);
            v.AddMember("ssao_radius", p.ssao_radius, alloc);
            v.AddMember("ssao_bias", p.ssao_bias, alloc);
            v.AddMember("ssao_intensity", p.ssao_intensity, alloc);
            v.AddMember("ssao_power", p.ssao_power, alloc);
            v.AddMember("ssao_samples", p.ssao_samples, alloc);
            v.AddMember("fxaa", p.fxaa, alloc);
            v.AddMember("taa", p.taa, alloc);
            v.AddMember("cas_sharpening", p.cas_sharpening, alloc);
            v.AddMember("cas_sharpness", p.cas_sharpness, alloc);
            v.AddMember("ssr", p.ssr, alloc);
            v.AddMember("tonemapping", p.tonemapping, alloc);
            v.AddMember("color_grading", p.color_grading, alloc);
            v.AddMember("color_grading_lift_r", p.color_grading_lift_r, alloc);
            v.AddMember("color_grading_lift_g", p.color_grading_lift_g, alloc);
            v.AddMember("color_grading_lift_b", p.color_grading_lift_b, alloc);
            v.AddMember("color_grading_gamma_r", p.color_grading_gamma_r, alloc);
            v.AddMember("color_grading_gamma_g", p.color_grading_gamma_g, alloc);
            v.AddMember("color_grading_gamma_b", p.color_grading_gamma_b, alloc);
            v.AddMember("color_grading_gain_r", p.color_grading_gain_r, alloc);
            v.AddMember("color_grading_gain_g", p.color_grading_gain_g, alloc);
            v.AddMember("color_grading_gain_b", p.color_grading_gain_b, alloc);
            v.AddMember("color_grading_saturation", p.color_grading_saturation, alloc);
            v.AddMember("color_grading_contrast", p.color_grading_contrast, alloc);
            v.AddMember("color_grading_intensity", p.color_grading_intensity, alloc);
            v.AddMember("dof", p.dof, alloc);
            v.AddMember("dof_focus_scale", p.dof_focus_scale, alloc);
            v.AddMember("dof_blur_range", p.dof_blur_range, alloc);
            v.AddMember("bloom", p.bloom, alloc);
            v.AddMember("bloom_strength", p.bloom_strength, alloc);
            v.AddMember("bloom_range", p.bloom_range, alloc);
            v.AddMember("motion_blur", p.motion_blur, alloc);
            v.AddMember("motion_blur_strength", p.motion_blur_strength, alloc);
            v.AddMember("motion_blur_samples", p.motion_blur_samples, alloc);
            v.AddMember("IBL", p.IBL, alloc);
            v.AddMember("IBL_intensity", p.IBL_intensity, alloc);
        }

        void ApplyPostProcessProfileMembers(const rapidjson::Value &v, PostProcessProfile &p)
        {
            auto B = [&](const char *k, bool &f)
            { if (v.HasMember(k)) f = v[k].GetBool(); };
            auto F = [&](const char *k, float &f)
            { if (v.HasMember(k)) f = v[k].GetFloat(); };
            auto I = [&](const char *k, int &f)
            { if (v.HasMember(k)) f = v[k].GetInt(); };
            B("ssao", p.ssao);
            F("ssao_radius", p.ssao_radius);
            F("ssao_bias", p.ssao_bias);
            F("ssao_intensity", p.ssao_intensity);
            F("ssao_power", p.ssao_power);
            I("ssao_samples", p.ssao_samples);
            B("fxaa", p.fxaa);
            B("taa", p.taa);
            B("cas_sharpening", p.cas_sharpening);
            F("cas_sharpness", p.cas_sharpness);
            B("ssr", p.ssr);
            B("tonemapping", p.tonemapping);
            B("color_grading", p.color_grading);
            F("color_grading_lift_r", p.color_grading_lift_r);
            F("color_grading_lift_g", p.color_grading_lift_g);
            F("color_grading_lift_b", p.color_grading_lift_b);
            F("color_grading_gamma_r", p.color_grading_gamma_r);
            F("color_grading_gamma_g", p.color_grading_gamma_g);
            F("color_grading_gamma_b", p.color_grading_gamma_b);
            F("color_grading_gain_r", p.color_grading_gain_r);
            F("color_grading_gain_g", p.color_grading_gain_g);
            F("color_grading_gain_b", p.color_grading_gain_b);
            F("color_grading_saturation", p.color_grading_saturation);
            F("color_grading_contrast", p.color_grading_contrast);
            F("color_grading_intensity", p.color_grading_intensity);
            B("dof", p.dof);
            F("dof_focus_scale", p.dof_focus_scale);
            F("dof_blur_range", p.dof_blur_range);
            B("bloom", p.bloom);
            F("bloom_strength", p.bloom_strength);
            F("bloom_range", p.bloom_range);
            B("motion_blur", p.motion_blur);
            F("motion_blur_strength", p.motion_blur_strength);
            I("motion_blur_samples", p.motion_blur_samples);
            B("IBL", p.IBL);
            F("IBL_intensity", p.IBL_intensity);
        }

        void AddSceneSettingsMembers(rapidjson::Value &settings, rapidjson::Document::AllocatorType &allocator)
        {
            auto &gSettings = Settings::Get<SceneSettings>();
            settings.AddMember("right_handed", gSettings.right_handed, allocator);
            settings.AddMember("reverse_depth", gSettings.reverse_depth, allocator);
            settings.AddMember("frustum_culling", gSettings.frustum_culling, allocator);
            settings.AddMember("occlusion_culling", gSettings.occlusion_culling, allocator);
            settings.AddMember("occlusion_culling_bias", gSettings.occlusion_culling_bias, allocator);
            settings.AddMember("lod_enabled", gSettings.lod_enabled, allocator);
            settings.AddMember("lod_count", gSettings.lod_count, allocator);
            settings.AddMember("lod_bias", gSettings.lod_bias, allocator);
            {
                rapidjson::Value lodDist(rapidjson::kArrayType);
                lodDist.PushBack(gSettings.lod_distances[0], allocator);
                lodDist.PushBack(gSettings.lod_distances[1], allocator);
                lodDist.PushBack(gSettings.lod_distances[2], allocator);
                settings.AddMember("lod_distances", lodDist.Move(), allocator);
            }
            settings.AddMember("shadows", gSettings.shadows, allocator);
            settings.AddMember("shadow_map_size", gSettings.shadow_map_size, allocator);
            settings.AddMember("num_cascades", gSettings.num_cascades, allocator);
            settings.AddMember("shadow_distance", gSettings.shadow_distance, allocator);
            settings.AddMember("shadow_cascade_lambda", gSettings.shadow_cascade_lambda, allocator);
            settings.AddMember("shadow_normal_bias", gSettings.shadow_normal_bias, allocator);
            settings.AddMember("shadow_fade_fraction", gSettings.shadow_fade_fraction, allocator);
            settings.AddMember("shadow_filter_radius", gSettings.shadow_filter_radius, allocator);
            settings.AddMember("shadow_debug_mode", gSettings.shadow_debug_mode, allocator);
            settings.AddMember("fog", gSettings.fog, allocator);
            settings.AddMember("fog_density", gSettings.fog_density, allocator);
            settings.AddMember("fog_start", gSettings.fog_start, allocator);
            settings.AddMember("render_scale", gSettings.render_scale, allocator);
            settings.AddMember("ssao", gSettings.ssao, allocator);
            settings.AddMember("ssao_radius", gSettings.ssao_radius, allocator);
            settings.AddMember("ssao_bias", gSettings.ssao_bias, allocator);
            settings.AddMember("ssao_intensity", gSettings.ssao_intensity, allocator);
            settings.AddMember("ssao_power", gSettings.ssao_power, allocator);
            settings.AddMember("ssao_samples", gSettings.ssao_samples, allocator);
            settings.AddMember("forward_plus", gSettings.forward_plus, allocator);
            settings.AddMember("fxaa", gSettings.fxaa, allocator);
            settings.AddMember("taa", gSettings.taa, allocator);
            settings.AddMember("cas_sharpening", gSettings.cas_sharpening, allocator);
            settings.AddMember("cas_sharpness", gSettings.cas_sharpness, allocator);
            settings.AddMember("ssr", gSettings.ssr, allocator);
            settings.AddMember("tonemapping", gSettings.tonemapping, allocator);
            settings.AddMember("color_grading", gSettings.color_grading, allocator);
            settings.AddMember("color_grading_lift_r", gSettings.color_grading_lift_r, allocator);
            settings.AddMember("color_grading_lift_g", gSettings.color_grading_lift_g, allocator);
            settings.AddMember("color_grading_lift_b", gSettings.color_grading_lift_b, allocator);
            settings.AddMember("color_grading_gamma_r", gSettings.color_grading_gamma_r, allocator);
            settings.AddMember("color_grading_gamma_g", gSettings.color_grading_gamma_g, allocator);
            settings.AddMember("color_grading_gamma_b", gSettings.color_grading_gamma_b, allocator);
            settings.AddMember("color_grading_gain_r", gSettings.color_grading_gain_r, allocator);
            settings.AddMember("color_grading_gain_g", gSettings.color_grading_gain_g, allocator);
            settings.AddMember("color_grading_gain_b", gSettings.color_grading_gain_b, allocator);
            settings.AddMember("color_grading_saturation", gSettings.color_grading_saturation, allocator);
            settings.AddMember("color_grading_contrast", gSettings.color_grading_contrast, allocator);
            settings.AddMember("color_grading_intensity", gSettings.color_grading_intensity, allocator);
            settings.AddMember("dof", gSettings.dof, allocator);
            settings.AddMember("dof_focus_scale", gSettings.dof_focus_scale, allocator);
            settings.AddMember("dof_blur_range", gSettings.dof_blur_range, allocator);
            settings.AddMember("bloom", gSettings.bloom, allocator);
            settings.AddMember("bloom_strength", gSettings.bloom_strength, allocator);
            settings.AddMember("bloom_range", gSettings.bloom_range, allocator);
            settings.AddMember("motion_blur", gSettings.motion_blur, allocator);
            settings.AddMember("motion_blur_strength", gSettings.motion_blur_strength, allocator);
            settings.AddMember("motion_blur_samples", gSettings.motion_blur_samples, allocator);
            settings.AddMember("IBL", gSettings.IBL, allocator);
            settings.AddMember("IBL_intensity", gSettings.IBL_intensity, allocator);
            settings.AddMember("lights_intensity", gSettings.lights_intensity, allocator);
            settings.AddMember("randomize_lights", gSettings.randomize_lights, allocator);
            settings.AddMember("skybox_path", rapidjson::Value(gSettings.skybox_path.c_str(), allocator), allocator);

            rapidjson::Value depthBias(rapidjson::kArrayType);
            depthBias.PushBack(gSettings.depth_bias[0], allocator);
            depthBias.PushBack(gSettings.depth_bias[1], allocator);
            depthBias.PushBack(gSettings.depth_bias[2], allocator);
            settings.AddMember("depth_bias", depthBias.Move(), allocator);

            settings.AddMember("time_scale", gSettings.time_scale, allocator);
            AddStringVectorMember(settings, "model_list", gSettings.model_list, allocator);
            settings.AddMember("freeze_frustum_culling", gSettings.freeze_frustum_culling, allocator);
            settings.AddMember("draw_aabbs", gSettings.draw_aabbs, allocator);
            settings.AddMember("draw_grid", gSettings.draw_grid, allocator);
            settings.AddMember("aabbs_depth_aware", gSettings.aabbs_depth_aware, allocator);
            settings.AddMember("selection_outline", gSettings.selection_outline, allocator);
            settings.AddMember("selection_outline_color_r", gSettings.selection_outline_color_r, allocator);
            settings.AddMember("selection_outline_color_g", gSettings.selection_outline_color_g, allocator);
            settings.AddMember("selection_outline_color_b", gSettings.selection_outline_color_b, allocator);
            settings.AddMember("selection_outline_color_a", gSettings.selection_outline_color_a, allocator);
            settings.AddMember("selection_outline_thickness", gSettings.selection_outline_thickness, allocator);
            settings.AddMember("selection_outline_inner_fade", gSettings.selection_outline_inner_fade, allocator);
            settings.AddMember("selection_outline_outer_fade", gSettings.selection_outline_outer_fade, allocator);
            settings.AddMember("dynamic_rendering", gSettings.dynamic_rendering, allocator);
            settings.AddMember("ray_tracing_support", gSettings.ray_tracing_support, allocator);
            settings.AddMember("render_mode", static_cast<int>(gSettings.render_mode), allocator);
            settings.AddMember("use_Disney_PBR", gSettings.use_Disney_PBR, allocator);
            settings.AddMember("physical_point_falloff", gSettings.physical_point_falloff, allocator);
            settings.AddMember("present_mode", static_cast<int>(gSettings.preferred_present_mode), allocator);
            settings.AddMember("scene_view_aspect_mode", static_cast<int>(gSettings.scene_view_aspect_mode), allocator);
        }

        SceneViewAspectMode ClampSceneViewAspectMode(int mode)
        {
            switch (static_cast<SceneViewAspectMode>(mode))
            {
            case SceneViewAspectMode::Free:
            case SceneViewAspectMode::Landscape16x9:
            case SceneViewAspectMode::Portrait9x16:
            case SceneViewAspectMode::Landscape19_5x9:
            case SceneViewAspectMode::Portrait9x19_5:
            case SceneViewAspectMode::Square1x1:
                return static_cast<SceneViewAspectMode>(mode);
            default:
                return SceneViewAspectMode::Free;
            }
        }

        // Parse the top-level "scene_scripts" object into a manifest. Always clears first, so a
        // scene without the member (or with an empty one) resets any previously-loaded manifest.
        void ParseSceneScriptManifest(const rapidjson::Value &doc, SceneScriptManifest &out)
        {
            out.Clear();
            if (!doc.HasMember("scene_scripts") || !doc["scene_scripts"].IsObject())
                return;
            const auto &obj = doc["scene_scripts"];

            if (obj.HasMember("on_play") && obj["on_play"].IsArray())
            {
                for (const auto &v : obj["on_play"].GetArray())
                    if (v.IsString())
                        out.onPlay.emplace_back(v.GetString());
            }

            if (obj.HasMember("actions") && obj["actions"].IsObject())
            {
                // Iterate members explicitly: GetObject() collides with the GetObjectW macro from
                // windows.h on this platform.
                const auto &actions = obj["actions"];
                for (auto m = actions.MemberBegin(); m != actions.MemberEnd(); ++m)
                {
                    if (!m->value.IsObject())
                        continue;
                    SceneScriptAction a;
                    a.id = m->name.GetString();
                    if (m->value.HasMember("script") && m->value["script"].IsString())
                        a.script = m->value["script"].GetString();
                    if (m->value.HasMember("function") && m->value["function"].IsString())
                        a.function = m->value["function"].GetString();
                    if (!a.id.empty())
                        out.actions.push_back(std::move(a));
                }
            }
        }

        void ApplySceneSettingsMembers(const rapidjson::Value &settings)
        {
            auto &gSettings = Settings::Get<SceneSettings>();
            const std::string previousSkyboxPath = gSettings.skybox_path;
            if (settings.HasMember("right_handed") && settings["right_handed"].GetBool() != gSettings.right_handed)
            {
                PE_WARN("[Scene] Ignoring saved right_handed=%s; this is fixed by the active runtime/backend setup",
                        settings["right_handed"].GetBool() ? "true" : "false");
            }
            if (settings.HasMember("reverse_depth") && settings["reverse_depth"].GetBool() != gSettings.reverse_depth)
            {
                PE_WARN("[Scene] Ignoring saved reverse_depth=%s; this is fixed by the active runtime/backend setup",
                        settings["reverse_depth"].GetBool() ? "true" : "false");
            }
            if (settings.HasMember("frustum_culling"))
                gSettings.frustum_culling = settings["frustum_culling"].GetBool();
            if (settings.HasMember("occlusion_culling"))
                gSettings.occlusion_culling = settings["occlusion_culling"].GetBool();
            if (settings.HasMember("occlusion_culling_bias"))
                gSettings.occlusion_culling_bias = settings["occlusion_culling_bias"].GetFloat();
            if (settings.HasMember("lod_enabled"))
                gSettings.lod_enabled = settings["lod_enabled"].GetBool();
            if (settings.HasMember("lod_count"))
                gSettings.lod_count = settings["lod_count"].GetUint();
            if (settings.HasMember("lod_bias"))
                gSettings.lod_bias = settings["lod_bias"].GetFloat();
            if (settings.HasMember("lod_distances") && settings["lod_distances"].IsArray() &&
                settings["lod_distances"].Size() == 3)
            {
                gSettings.lod_distances[0] = settings["lod_distances"][0].GetFloat();
                gSettings.lod_distances[1] = settings["lod_distances"][1].GetFloat();
                gSettings.lod_distances[2] = settings["lod_distances"][2].GetFloat();
            }
            if (settings.HasMember("fog"))
                gSettings.fog = settings["fog"].GetBool();
            if (settings.HasMember("fog_density"))
                gSettings.fog_density = settings["fog_density"].GetFloat();
            if (settings.HasMember("fog_start"))
                gSettings.fog_start = settings["fog_start"].GetFloat();
            if (settings.HasMember("shadows"))
                gSettings.shadows = settings["shadows"].GetBool();
            if (settings.HasMember("shadow_map_size"))
                gSettings.shadow_map_size = settings["shadow_map_size"].GetUint();
            if (settings.HasMember("num_cascades"))
                gSettings.num_cascades = settings["num_cascades"].GetUint();
            if (settings.HasMember("shadow_distance"))
                gSettings.shadow_distance = settings["shadow_distance"].GetFloat();
            if (settings.HasMember("shadow_cascade_lambda"))
                gSettings.shadow_cascade_lambda = settings["shadow_cascade_lambda"].GetFloat();
            if (settings.HasMember("shadow_normal_bias"))
                gSettings.shadow_normal_bias = settings["shadow_normal_bias"].GetFloat();
            if (settings.HasMember("shadow_fade_fraction"))
                gSettings.shadow_fade_fraction = settings["shadow_fade_fraction"].GetFloat();
            if (settings.HasMember("shadow_filter_radius"))
                gSettings.shadow_filter_radius = settings["shadow_filter_radius"].GetFloat();
            if (settings.HasMember("shadow_debug_mode"))
                gSettings.shadow_debug_mode = settings["shadow_debug_mode"].GetInt();
            if (settings.HasMember("render_scale"))
            {
                const float renderScale = settings["render_scale"].GetFloat();
                if (gSettings.render_scale != renderScale)
                {
                    gSettings.render_scale = renderScale;
                    EventSystem::PushEvent(EventType::Resize);
                }
            }
            if (settings.HasMember("ssao"))
                gSettings.ssao = settings["ssao"].GetBool();
            if (settings.HasMember("ssao_radius"))
                gSettings.ssao_radius = settings["ssao_radius"].GetFloat();
            if (settings.HasMember("ssao_bias"))
                gSettings.ssao_bias = settings["ssao_bias"].GetFloat();
            if (settings.HasMember("ssao_intensity"))
                gSettings.ssao_intensity = settings["ssao_intensity"].GetFloat();
            if (settings.HasMember("ssao_power"))
                gSettings.ssao_power = settings["ssao_power"].GetFloat();
            if (settings.HasMember("ssao_samples"))
                gSettings.ssao_samples = settings["ssao_samples"].GetInt();
            if (settings.HasMember("forward_plus"))
                gSettings.forward_plus = settings["forward_plus"].GetBool();
            if (settings.HasMember("fxaa"))
                gSettings.fxaa = settings["fxaa"].GetBool();
            if (settings.HasMember("taa"))
                gSettings.taa = settings["taa"].GetBool();
            if (settings.HasMember("cas_sharpening"))
                gSettings.cas_sharpening = settings["cas_sharpening"].GetBool();
            if (settings.HasMember("cas_sharpness"))
                gSettings.cas_sharpness = settings["cas_sharpness"].GetFloat();
            if (settings.HasMember("ssr"))
                gSettings.ssr = settings["ssr"].GetBool();
            if (settings.HasMember("tonemapping"))
                gSettings.tonemapping = settings["tonemapping"].GetBool();
            if (settings.HasMember("color_grading"))
                gSettings.color_grading = settings["color_grading"].GetBool();
            if (settings.HasMember("color_grading_lift_r"))
                gSettings.color_grading_lift_r = settings["color_grading_lift_r"].GetFloat();
            if (settings.HasMember("color_grading_lift_g"))
                gSettings.color_grading_lift_g = settings["color_grading_lift_g"].GetFloat();
            if (settings.HasMember("color_grading_lift_b"))
                gSettings.color_grading_lift_b = settings["color_grading_lift_b"].GetFloat();
            if (settings.HasMember("color_grading_gamma_r"))
                gSettings.color_grading_gamma_r = settings["color_grading_gamma_r"].GetFloat();
            if (settings.HasMember("color_grading_gamma_g"))
                gSettings.color_grading_gamma_g = settings["color_grading_gamma_g"].GetFloat();
            if (settings.HasMember("color_grading_gamma_b"))
                gSettings.color_grading_gamma_b = settings["color_grading_gamma_b"].GetFloat();
            if (settings.HasMember("color_grading_gain_r"))
                gSettings.color_grading_gain_r = settings["color_grading_gain_r"].GetFloat();
            if (settings.HasMember("color_grading_gain_g"))
                gSettings.color_grading_gain_g = settings["color_grading_gain_g"].GetFloat();
            if (settings.HasMember("color_grading_gain_b"))
                gSettings.color_grading_gain_b = settings["color_grading_gain_b"].GetFloat();
            if (settings.HasMember("color_grading_saturation"))
                gSettings.color_grading_saturation = settings["color_grading_saturation"].GetFloat();
            if (settings.HasMember("color_grading_contrast"))
                gSettings.color_grading_contrast = settings["color_grading_contrast"].GetFloat();
            if (settings.HasMember("color_grading_intensity"))
                gSettings.color_grading_intensity = settings["color_grading_intensity"].GetFloat();
            if (settings.HasMember("dof"))
                gSettings.dof = settings["dof"].GetBool();
            if (settings.HasMember("dof_focus_scale"))
                gSettings.dof_focus_scale = settings["dof_focus_scale"].GetFloat();
            if (settings.HasMember("dof_blur_range"))
                gSettings.dof_blur_range = settings["dof_blur_range"].GetFloat();
            if (settings.HasMember("bloom"))
                gSettings.bloom = settings["bloom"].GetBool();
            if (settings.HasMember("bloom_strength"))
                gSettings.bloom_strength = settings["bloom_strength"].GetFloat();
            if (settings.HasMember("bloom_range"))
                gSettings.bloom_range = settings["bloom_range"].GetFloat();
            if (settings.HasMember("motion_blur"))
                gSettings.motion_blur = settings["motion_blur"].GetBool();
            if (settings.HasMember("motion_blur_strength"))
                gSettings.motion_blur_strength = settings["motion_blur_strength"].GetFloat();
            if (settings.HasMember("motion_blur_samples"))
                gSettings.motion_blur_samples = settings["motion_blur_samples"].GetInt();
            if (settings.HasMember("IBL"))
                gSettings.IBL = settings["IBL"].GetBool();
            if (settings.HasMember("IBL_intensity"))
                gSettings.IBL_intensity = settings["IBL_intensity"].GetFloat();
            if (settings.HasMember("lights_intensity"))
                gSettings.lights_intensity = settings["lights_intensity"].GetFloat();
            if (settings.HasMember("randomize_lights"))
                gSettings.randomize_lights = settings["randomize_lights"].GetBool();
            gSettings.skybox_path = settings.HasMember("skybox_path") && settings["skybox_path"].IsString()
                                        ? settings["skybox_path"].GetString()
                                        : SceneSettings::DefaultSkyboxPath;
            if (gSettings.skybox_path != previousSkyboxPath)
                RefreshSceneSky();
            if (settings.HasMember("depth_bias") && settings["depth_bias"].IsArray() && settings["depth_bias"].Size() == 3)
            {
                gSettings.depth_bias[0] = settings["depth_bias"][0].GetFloat();
                gSettings.depth_bias[1] = settings["depth_bias"][1].GetFloat();
                gSettings.depth_bias[2] = settings["depth_bias"][2].GetFloat();
            }
            if (settings.HasMember("time_scale"))
                gSettings.time_scale = settings["time_scale"].GetFloat();
            ReadStringVectorMember(settings, "model_list", gSettings.model_list);
            if (settings.HasMember("freeze_frustum_culling"))
                gSettings.freeze_frustum_culling = settings["freeze_frustum_culling"].GetBool();
            if (settings.HasMember("draw_grid"))
                gSettings.draw_grid = settings["draw_grid"].GetBool();
            if (settings.HasMember("draw_aabbs"))
                gSettings.draw_aabbs = settings["draw_aabbs"].GetBool();
            if (settings.HasMember("aabbs_depth_aware"))
                gSettings.aabbs_depth_aware = settings["aabbs_depth_aware"].GetBool();
            if (settings.HasMember("selection_outline"))
                gSettings.selection_outline = settings["selection_outline"].GetBool();
            if (settings.HasMember("selection_outline_color_r"))
                gSettings.selection_outline_color_r = settings["selection_outline_color_r"].GetFloat();
            if (settings.HasMember("selection_outline_color_g"))
                gSettings.selection_outline_color_g = settings["selection_outline_color_g"].GetFloat();
            if (settings.HasMember("selection_outline_color_b"))
                gSettings.selection_outline_color_b = settings["selection_outline_color_b"].GetFloat();
            if (settings.HasMember("selection_outline_color_a"))
                gSettings.selection_outline_color_a = settings["selection_outline_color_a"].GetFloat();
            if (settings.HasMember("selection_outline_thickness"))
                gSettings.selection_outline_thickness = settings["selection_outline_thickness"].GetFloat();
            if (settings.HasMember("selection_outline_inner_fade"))
                gSettings.selection_outline_inner_fade = settings["selection_outline_inner_fade"].GetFloat();
            if (settings.HasMember("selection_outline_outer_fade"))
                gSettings.selection_outline_outer_fade = settings["selection_outline_outer_fade"].GetFloat();
            if (settings.HasMember("dynamic_rendering"))
                gSettings.dynamic_rendering = settings["dynamic_rendering"].GetBool() && RHII.GetCaps().dynamicRendering;
            if (settings.HasMember("ray_tracing_support"))
                gSettings.ray_tracing_support = RHII.GetCaps().rayTracing;
            if (settings.HasMember("render_mode"))
                gSettings.render_mode = ClampRenderModeToRayTracingSupport(
                    static_cast<RenderMode>(settings["render_mode"].GetInt()), RHII.GetCaps().rayTracing);
            if (settings.HasMember("use_Disney_PBR"))
                gSettings.use_Disney_PBR = settings["use_Disney_PBR"].GetBool();
            if (settings.HasMember("physical_point_falloff"))
                gSettings.physical_point_falloff = settings["physical_point_falloff"].GetBool();
            if (settings.HasMember("present_mode"))
            {
                gSettings.preferred_present_mode = static_cast<PePresentMode>(settings["present_mode"].GetInt());
                EventSystem::PushEvent(EventType::PresentMode);
            }
            if (settings.HasMember("scene_view_aspect_mode") && settings["scene_view_aspect_mode"].IsInt())
                gSettings.scene_view_aspect_mode = ClampSceneViewAspectMode(settings["scene_view_aspect_mode"].GetInt());
        }

        std::string MakePrimitiveSourceId(const std::string &ptype, const vec4 &params, uint32_t count)
        {
            std::string id = "prim:" + ptype;
            for (uint32_t i = 0; i < count && i < 4; ++i)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6f", params[i]);
                id += ":";
                id += buf;
            }
            return id;
        }

        struct SkinnedAnimationStats
        {
            int nodes = 0;
            size_t boneRefs = 0;
            size_t clipRefs = 0;
        };

        std::filesystem::path ResolveSerializedFilePath(const rapidjson::Value &pathValue,
                                                        const std::filesystem::path *relativeToDir);

        SkinnedAnimationStats GetSkinnedAnimationStats(const Scene &scene)
        {
            SkinnedAnimationStats stats;
            for (uint32_t ni = 0; ni < scene.GetNodeCount(); ++ni)
            {
                const NodeId *node = scene.GetNodeId(ni);
                if (node && scene.NodeHasSkinnedMesh(node))
                {
                    ++stats.nodes;
                    stats.boneRefs += scene.GetSkeletonForNode(node).bones.size();
                    stats.clipRefs += scene.GetAnimationClipsForNode(node).size();
                }
            }

            return stats;
        }

        void RestoreSkyboxNode(Scene &scene, NodeId *node, const rapidjson::Value &nodeValue)
        {
            if (!node || !nodeValue.HasMember("skybox") || !nodeValue["skybox"].IsObject())
                return;

            const auto &sv = nodeValue["skybox"];
            auto &settings = Settings::Get<SceneSettings>();
            const std::string path = sv.HasMember("path") && sv["path"].IsString()
                                         ? sv["path"].GetString()
                                         : settings.skybox_path;

            scene.AddComponentFlag(node, Component_Skybox);
            scene.SetSkyboxPath(node, path, false);
        }

        void RestoreVoxelWorldNode(Scene &scene, NodeId *node, const rapidjson::Value &nodeValue)
        {
            if (!node || !nodeValue.HasMember("voxelWorld") || !nodeValue["voxelWorld"].IsObject())
                return;

            const auto &vv = nodeValue["voxelWorld"];
            scene.AddComponentFlag(node, Component_VoxelWorld);
            NodeVoxelWorldTag *v = scene.GetVoxelWorldForNode(node);
            if (!v)
                return;

            if (vv.HasMember("enabled"))
                v->worldEnabled = vv["enabled"].GetBool();
            if (vv.HasMember("streaming"))
                v->streaming = vv["streaming"].GetBool();
            if (vv.HasMember("anchorFollowsCamera"))
                v->anchorFollowsCamera = vv["anchorFollowsCamera"].GetBool();
            if (vv.HasMember("loadRadius"))
                v->loadRadius = vv["loadRadius"].GetInt();
            if (vv.HasMember("unloadMargin"))
                v->unloadMargin = vv["unloadMargin"].GetInt();
            if (vv.HasMember("uploadBudget"))
                v->uploadBudget = vv["uploadBudget"].GetInt();
            if (vv.HasMember("groundY"))
                v->groundY = vv["groundY"].GetInt();
            if (vv.HasMember("worldRadius"))
                v->worldRadius = vv["worldRadius"].GetInt();
            if (vv.HasMember("lodEnabled"))
                v->lodEnabled = vv["lodEnabled"].GetBool();
            if (vv.HasMember("lod0Radius"))
                v->lod0Radius = vv["lod0Radius"].GetInt();
            if (vv.HasMember("saveDir") && vv["saveDir"].IsString())
                v->saveDir = vv["saveDir"].GetString();
            if (vv.HasMember("noiseAmplitude"))
                v->noiseAmplitude = vv["noiseAmplitude"].GetFloat();
            if (vv.HasMember("noiseFeatureScale"))
                v->noiseFeatureScale = vv["noiseFeatureScale"].GetFloat();
            if (vv.HasMember("noiseSeed"))
                v->noiseSeed = vv["noiseSeed"].GetInt();
            if (vv.HasMember("caves"))
                v->caves = vv["caves"].GetBool();
            if (vv.HasMember("seaLevel"))
                v->seaLevel = vv["seaLevel"].GetInt();
            if (vv.HasMember("heightmapPath") && vv["heightmapPath"].IsString())
                v->heightmapPath = vv["heightmapPath"].GetString();
            if (vv.HasMember("strata1Path") && vv["strata1Path"].IsString())
                v->strata1Path = vv["strata1Path"].GetString();
            if (vv.HasMember("strata2Path") && vv["strata2Path"].IsString())
                v->strata2Path = vv["strata2Path"].GetString();
            if (vv.HasMember("featuresPath") && vv["featuresPath"].IsString())
                v->featuresPath = vv["featuresPath"].GetString();
            if (vv.HasMember("blocksPerPixel"))
                v->blocksPerPixel = vv["blocksPerPixel"].GetInt();
            if (vv.HasMember("heightMin"))
                v->heightMin = vv["heightMin"].GetFloat();
            if (vv.HasMember("heightMax"))
                v->heightMax = vv["heightMax"].GetFloat();
            if (vv.HasMember("groundHeight"))
                v->groundHeight = vv["groundHeight"].GetFloat();
            if (vv.HasMember("seaLevelM"))
                v->seaLevelM = vv["seaLevelM"].GetFloat();
            if (vv.HasMember("smooth"))
                v->smooth = vv["smooth"].GetBool();
            if (vv.HasMember("physics"))
                v->physics = vv["physics"].GetBool();
            if (vv.HasMember("physicsFriction"))
                v->physicsFriction = vv["physicsFriction"].GetFloat();
            if (vv.HasMember("physicsRestitution"))
                v->physicsRestitution = vv["physicsRestitution"].GetFloat();
            if (vv.HasMember("autoRebuild"))
                v->autoRebuild = vv["autoRebuild"].GetBool();
            if (vv.HasMember("surfaceBlock"))
                v->surfaceBlock = vv["surfaceBlock"].GetInt();
            if (vv.HasMember("surfaceBands"))
                v->surfaceBands = vv["surfaceBands"].GetBool();
            if (vv.HasMember("strata1Block"))
                v->strata1Block = vv["strata1Block"].GetInt();
            if (vv.HasMember("strata2Block"))
                v->strata2Block = vv["strata2Block"].GetInt();
            if (vv.HasMember("fillBlock"))
                v->fillBlock = vv["fillBlock"].GetInt();
            if (vv.HasMember("strata1Thickness"))
                v->strata1Thickness = vv["strata1Thickness"].GetInt();
            if (vv.HasMember("strata2Thickness"))
                v->strata2Thickness = vv["strata2Thickness"].GetInt();
        }

        void RestoreTriggerZoneNode(Scene &scene, NodeId *node, const rapidjson::Value &nodeValue)
        {
            if (!node || !nodeValue.HasMember("triggerZone") || !nodeValue["triggerZone"].IsObject())
                return;

            const auto &zv = nodeValue["triggerZone"];
            scene.AddComponentFlag(node, Component_TriggerZone);
            NodeTriggerZoneTag *z = scene.GetTriggerZoneForNode(node);
            if (!z)
                return;

            // common
            if (zv.HasMember("priority"))
                z->priority = zv["priority"].GetFloat();
            if (zv.HasMember("blend"))
                z->blend = zv["blend"].GetFloat();
            if (zv.HasMember("blend_distance"))
                z->blend_distance = zv["blend_distance"].GetFloat();
            if (zv.HasMember("runMode"))
                z->runMode = static_cast<TriggerRunMode>(zv["runMode"].GetInt());
            if (zv.HasMember("shape"))
                z->shape = static_cast<ZoneShape>(zv["shape"].GetInt());
            // script section
            if (zv.HasMember("scriptEnabled"))
                z->scriptEnabled = zv["scriptEnabled"].GetBool();
            if (zv.HasMember("scriptPath") && zv["scriptPath"].IsString())
                z->scriptPath = zv["scriptPath"].GetString();
            if (zv.HasMember("fireForCamera"))
                z->fireForCamera = zv["fireForCamera"].GetBool();
            if (zv.HasMember("onEnter") && zv["onEnter"].IsString())
                z->onEnter = zv["onEnter"].GetString();
            if (zv.HasMember("onExit") && zv["onExit"].IsString())
                z->onExit = zv["onExit"].GetString();
            // post-process section
            if (zv.HasMember("postProcessEnabled"))
                z->postProcessEnabled = zv["postProcessEnabled"].GetBool();
            if (zv.HasMember("postProcess") && zv["postProcess"].IsObject())
                ApplyPostProcessProfileMembers(zv["postProcess"], z->postProcess);
            // audio section (zone-owned source)
            if (zv.HasMember("audioEnabled"))
                z->audioEnabled = zv["audioEnabled"].GetBool();
            if (zv.HasMember("audioSource") && zv["audioSource"].IsObject())
            {
                const auto &a = zv["audioSource"];
                if (a.HasMember("file") && a["file"].IsString())
                    z->audioSource.filePath = a["file"].GetString();
                if (a.HasMember("volume"))
                    z->audioSource.volume = a["volume"].GetFloat();
                if (a.HasMember("pitch"))
                    z->audioSource.pitch = a["pitch"].GetFloat();
                if (a.HasMember("min_distance"))
                    z->audioSource.minDistance = a["min_distance"].GetFloat();
                if (a.HasMember("max_distance"))
                    z->audioSource.maxDistance = a["max_distance"].GetFloat();
                if (a.HasMember("loop"))
                    z->audioSource.loop = a["loop"].GetBool();
                if (a.HasMember("spatial"))
                    z->audioSource.spatial = a["spatial"].GetBool();
            }
            // physics section
            if (zv.HasMember("physicsEnabled"))
                z->physicsEnabled = zv["physicsEnabled"].GetBool();
            if (zv.HasMember("physicsEngine"))
                z->physicsEngine = static_cast<ZonePhysicsEngine>(zv["physicsEngine"].GetInt());
            if (zv.HasMember("physicsMode"))
                z->physicsMode = static_cast<ZonePhysicsMode>(zv["physicsMode"].GetInt());
            if (zv.HasMember("physicsBodyType"))
                z->physicsBodyType = static_cast<PhysicsBodyType>(zv["physicsBodyType"].GetInt());
            if (zv.HasMember("physicsMass"))
                z->physicsMass = zv["physicsMass"].GetFloat();
            if (zv.HasMember("physicsFriction"))
                z->physicsFriction = zv["physicsFriction"].GetFloat();
            if (zv.HasMember("physicsRestitution"))
                z->physicsRestitution = zv["physicsRestitution"].GetFloat();
            if (zv.HasMember("physicsFilterTag") && zv["physicsFilterTag"].IsString())
                z->physicsFilterTag = zv["physicsFilterTag"].GetString();
            if (zv.HasMember("physicsScriptPath") && zv["physicsScriptPath"].IsString())
                z->physicsScriptPath = zv["physicsScriptPath"].GetString();
            if (zv.HasMember("physicsOnEnter") && zv["physicsOnEnter"].IsString())
                z->physicsOnEnter = zv["physicsOnEnter"].GetString();
            if (zv.HasMember("physicsOnExit") && zv["physicsOnExit"].IsString())
                z->physicsOnExit = zv["physicsOnExit"].GetString();
            if (zv.HasMember("physicsForceField"))
                z->physicsForceField = zv["physicsForceField"].GetBool();
            if (zv.HasMember("physicsForceX"))
                z->physicsForce.x = zv["physicsForceX"].GetFloat();
            if (zv.HasMember("physicsForceY"))
                z->physicsForce.y = zv["physicsForceY"].GetFloat();
            if (zv.HasMember("physicsForceZ"))
                z->physicsForce.z = zv["physicsForceZ"].GetFloat();
            // spawn section
            if (zv.HasMember("spawnEnabled"))
                z->spawnEnabled = zv["spawnEnabled"].GetBool();
            if (zv.HasMember("spawnPrefabPath") && zv["spawnPrefabPath"].IsString())
                z->spawnPrefabPath = zv["spawnPrefabPath"].GetString();
            if (zv.HasMember("spawnDespawnOnExit"))
                z->spawnDespawnOnExit = zv["spawnDespawnOnExit"].GetBool();
            // streaming section
            if (zv.HasMember("streamEnabled"))
                z->streamEnabled = zv["streamEnabled"].GetBool();
            if (zv.HasMember("streamTargetName") && zv["streamTargetName"].IsString())
                z->streamTargetName = zv["streamTargetName"].GetString();
            // camera section
            if (zv.HasMember("cameraEnabled"))
                z->cameraEnabled = zv["cameraEnabled"].GetBool();
            if (zv.HasMember("cameraTargetName") && zv["cameraTargetName"].IsString())
                z->cameraTargetName = zv["cameraTargetName"].GetString();
            if (zv.HasMember("cameraFovDeg"))
                z->cameraFovDeg = zv["cameraFovDeg"].GetFloat();
        }

        void RestorePrefabNode(Scene &scene,
                               NodeId *node,
                               const rapidjson::Value &nodeValue,
                               const std::filesystem::path *relativeToDir)
        {
            if (!node || !scene.IsNodeAlive(node))
                return;

            if (!nodeValue.HasMember("prefab"))
            {
                scene.ClearNodePrefab(node);
                return;
            }

            std::filesystem::path prefabPath = ResolveSerializedFilePath(nodeValue["prefab"], relativeToDir);
            if (prefabPath.empty())
                scene.ClearNodePrefab(node);
            else
                scene.SetNodePrefabPath(node, prefabPath.generic_string(), false);
        }

        const char *RuntimeUiWidgetTypeToString(NodeRuntimeUiWidgetType type)
        {
            switch (type)
            {
            case NodeRuntimeUiWidgetType::Text:
                return "text";
            case NodeRuntimeUiWidgetType::Button:
                return "button";
            case NodeRuntimeUiWidgetType::Image:
                return "image";
            case NodeRuntimeUiWidgetType::Panel:
            default:
                return "panel";
            }
        }

        NodeRuntimeUiWidgetType RuntimeUiWidgetTypeFromString(const char *type)
        {
            if (!type)
                return NodeRuntimeUiWidgetType::Panel;

            std::string value = type;
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (value == "text" || value == "label")
                return NodeRuntimeUiWidgetType::Text;
            if (value == "button")
                return NodeRuntimeUiWidgetType::Button;
            if (value == "image")
                return NodeRuntimeUiWidgetType::Image;
            return NodeRuntimeUiWidgetType::Panel;
        }

        void RestoreRuntimeUiNode(Scene &scene,
                                  NodeId *node,
                                  const rapidjson::Value &nodeValue,
                                  const std::filesystem::path *relativeToDir)
        {
            if (!node || !scene.IsNodeAlive(node))
                return;

            const bool hasFlag = nodeValue.HasMember("component_flags") &&
                                 (nodeValue["component_flags"].GetUint() & Component_RuntimeUi) != 0;
            if (!nodeValue.HasMember("runtime_ui") || !nodeValue["runtime_ui"].IsObject())
            {
                if (!hasFlag)
                    scene.ClearRuntimeUiComponent(node);
                else if (NodeRuntimeUiTag *ui = scene.GetRuntimeUiComponent(node))
                    ui->authored = false;
                return;
            }

            const auto &uv = nodeValue["runtime_ui"];
            NodeRuntimeUiTag &ui = scene.GetOrCreateRuntimeUiComponent(node);

            auto readString = [&](const char *name, const std::string &fallback = {}) -> std::string
            {
                return uv.HasMember(name) && uv[name].IsString() ? uv[name].GetString() : fallback;
            };
            auto readFloat = [&](const char *name, float fallback) -> float
            {
                if (!uv.HasMember(name) || !uv[name].IsNumber())
                    return fallback;
                const float value = uv[name].GetFloat();
                return std::isfinite(value) ? value : fallback;
            };
            auto readBool = [&](const char *name, bool fallback) -> bool
            {
                return uv.HasMember(name) && uv[name].IsBool() ? uv[name].GetBool() : fallback;
            };
            auto readVec4 = [&](const char *name, const vec4 &fallback) -> vec4
            {
                if (!uv.HasMember(name) || !uv[name].IsArray() || uv[name].Size() < 4)
                    return fallback;
                const auto &arr = uv[name];
                if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber() || !arr[3].IsNumber())
                    return fallback;
                vec4 value(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat());
                return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w) ? value : fallback;
            };
            auto readVec2 = [&](const char *name, const vec2 &fallback) -> vec2
            {
                if (!uv.HasMember(name) || !uv[name].IsArray() || uv[name].Size() < 2)
                    return fallback;
                const auto &arr = uv[name];
                if (!arr[0].IsNumber() || !arr[1].IsNumber())
                    return fallback;
                vec2 value(arr[0].GetFloat(), arr[1].GetFloat());
                return std::isfinite(value.x) && std::isfinite(value.y) ? value : fallback;
            };

            ui.authored = true;
            ui.widgetType = RuntimeUiWidgetTypeFromString(readString("type", "panel").c_str());
            ui.screenId = readString("screen", "__scene_ui");
            ui.widgetId = readString("id");
            ui.label = readString("label");
            ui.title = readString("title");
            ui.subtitle = readString("subtitle");
            ui.body = readString("body");
            ui.footer = readString("footer");
            ui.imagePath = uv.HasMember("image") && uv["image"].IsString()
                               ? ResolveSerializedFilePath(uv["image"], relativeToDir).generic_string()
                               : "";
            ui.actionName = readString("action", "click");
            ui.actionFunction = readString("action_function");
            ui.fillColor = readVec4("fill", ui.fillColor);
            ui.borderColor = readVec4("border", ui.borderColor);
            ui.accentColor = readVec4("accent", ui.accentColor);
            ui.textColor = readVec4("text_color", ui.textColor);
            ui.imageTint = readVec4("image_tint", ui.imageTint);
            ui.anchor = readVec2("anchor", vec2(0.0f, 0.0f));
            ui.pivot = readVec2("pivot", vec2(0.5f, 0.5f));
            ui.fontScale = readFloat("font_scale", 1.0f);
            ui.textAlignH = static_cast<uint8_t>(readFloat("text_align_h", 0.0f));
            ui.textAlignV = static_cast<uint8_t>(readFloat("text_align_v", 0.0f));
            ui.textOffset = readVec2("text_offset", vec2(0.0f, 0.0f));
            ui.visible = readBool("visible", true);
            ui.draggable = readBool("draggable", false);
            ui.noInput = readBool("no_input", false);
            ui.bringToFront = readBool("bring_to_front", false);
        }

        void RestoreSpriteNode(Scene &scene,
                               NodeId *node,
                               const rapidjson::Value &nodeValue,
                               const std::filesystem::path *relativeToDir)
        {
            if (!node || !scene.IsNodeAlive(node))
                return;

            if (!nodeValue.HasMember("sprite") || !nodeValue["sprite"].IsObject())
            {
                if (!(nodeValue.HasMember("component_flags") && (nodeValue["component_flags"].GetUint() & Component_Sprite)))
                    scene.ClearSpriteComponent(node);
                return;
            }

            const auto &sv = nodeValue["sprite"];
            NodeSpriteComponent &sprite = scene.GetOrCreateSpriteComponent(node);

            auto readString = [&](const char *name) -> std::string
            {
                return sv.HasMember(name) && sv[name].IsString() ? sv[name].GetString() : "";
            };
            auto readInt = [&](const char *name, int fallback) -> int
            {
                return sv.HasMember(name) && sv[name].IsInt() ? sv[name].GetInt() : fallback;
            };
            auto readFloat = [&](const char *name, float fallback) -> float
            {
                if (!sv.HasMember(name) || !sv[name].IsNumber())
                    return fallback;
                const float value = sv[name].GetFloat();
                return std::isfinite(value) ? value : fallback;
            };
            auto readVec4 = [&](const char *name, const vec4 &fallback) -> vec4
            {
                if (!sv.HasMember(name) || !sv[name].IsArray() || sv[name].Size() < 4)
                    return fallback;
                const auto &arr = sv[name];
                if (!arr[0].IsNumber() || !arr[1].IsNumber() || !arr[2].IsNumber() || !arr[3].IsNumber())
                    return fallback;
                vec4 value(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat());
                return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w) ? value : fallback;
            };

            sprite.imagePath = sv.HasMember("image") && sv["image"].IsString()
                                   ? ResolveSerializedFilePath(sv["image"], relativeToDir).generic_string()
                                   : "";
            sprite.metadataPath = sv.HasMember("metadata") && sv["metadata"].IsString()
                                      ? ResolveSerializedFilePath(sv["metadata"], relativeToDir).generic_string()
                                      : "";
            sprite.frameName = readString("frame_name");
            sprite.frameIndex = readInt("frame_index", -1);
            sprite.imageWidth = readInt("image_width", 0);
            sprite.imageHeight = readInt("image_height", 0);
            sprite.frameWidth = readInt("frame_width", 0);
            sprite.frameHeight = readInt("frame_height", 0);
            sprite.quadWidth = readFloat("quad_width", 1.0f);
            sprite.quadHeight = readFloat("quad_height", 1.0f);
            sprite.uvRect = readVec4("uv_rect", sprite.uvRect);
            sprite.tint = readVec4("tint", sprite.tint);
            sprite.activeClipName = readString("active_clip");
            sprite.activeClipIndex = readInt("active_clip_index", -1);
            sprite.meshSlot = readInt("mesh_slot", 0);
            sprite.playbackAccumulator = 0.0f;
            sprite.playbackSpeed = readFloat("playback_speed", 1.0f);
            sprite.playing = false;
            sprite.loop = sv.HasMember("loop") && sv["loop"].IsBool() ? sv["loop"].GetBool() : true;
            sprite.metadataLoaded = false;
        }

        void RestoreSkinnedStrip2DNode(Scene &scene, NodeId *node, const rapidjson::Value &nodeValue)
        {
            if (!node || !scene.IsNodeAlive(node))
                return;

            if (!scene.NodeUsesSkinnedStrip2D(node) || !nodeValue.HasMember("skinned_strip_2d") ||
                !nodeValue["skinned_strip_2d"].IsObject())
            {
                scene.ClearSkinnedStrip2DState(node);
                return;
            }

            const auto &sv = nodeValue["skinned_strip_2d"];
            NodeSkinnedStrip2DComponent &state = scene.GetOrCreateSkinnedStrip2DState(node);

            auto readFloat = [&](const char *name, float fallback) -> float
            {
                if (!sv.HasMember(name) || !sv[name].IsNumber())
                    return fallback;
                const float value = sv[name].GetFloat();
                return std::isfinite(value) ? value : fallback;
            };

            auto readInt = [&](const char *name, int fallback) -> int
            {
                return sv.HasMember(name) && sv[name].IsInt() ? sv[name].GetInt() : fallback;
            };

            if (sv.HasMember("ik_target") && sv["ik_target"].IsArray() && sv["ik_target"].Size() >= 2 &&
                sv["ik_target"][0].IsNumber() && sv["ik_target"][1].IsNumber())
            {
                const vec2 target(sv["ik_target"][0].GetFloat(), sv["ik_target"][1].GetFloat());
                if (std::isfinite(target.x) && std::isfinite(target.y))
                    state.ikTargetLocal = target;
            }

            state.ikIterations = std::clamp(readInt("ik_iterations", state.ikIterations), 1, 64);
            state.maxBendDegrees = std::clamp(readFloat("max_bend_degrees", state.maxBendDegrees), 5.0f, 180.0f);
            state.bendSign = readFloat("bend_sign", state.bendSign) < 0.0f ? -1.0f : 1.0f;
            state.maxStretchScale = std::clamp(readFloat("max_stretch_scale", state.maxStretchScale), 1.0f, 3.0f);
            state.stretchScale = std::clamp(readFloat("stretch_scale", state.stretchScale), 1.0f, state.maxStretchScale);

            state.rotationsRadians.clear();
            if (sv.HasMember("rotations") && sv["rotations"].IsArray())
            {
                const auto &rotations = sv["rotations"];
                rapidjson::SizeType rotationCount = rotations.Size();
                const int jointCount = scene.GetJointCountForNode(node);
                if (jointCount > 0)
                    rotationCount = std::min(rotationCount, static_cast<rapidjson::SizeType>(jointCount));

                state.rotationsRadians.reserve(rotationCount);
                for (rapidjson::SizeType i = 0; i < rotationCount; ++i)
                {
                    const float rotation = rotations[i].IsNumber() ? rotations[i].GetFloat() : 0.0f;
                    state.rotationsRadians.push_back(std::isfinite(rotation) ? rotation : 0.0f);
                }
            }

            state.jointInfluences.clear();
            if (sv.HasMember("joint_influences") && sv["joint_influences"].IsArray())
            {
                const auto &influences = sv["joint_influences"];
                rapidjson::SizeType influenceCount = influences.Size();
                const int jointCount = scene.GetJointCountForNode(node);
                if (jointCount > 0)
                    influenceCount = std::min(influenceCount, static_cast<rapidjson::SizeType>(jointCount));

                state.jointInfluences.reserve(influenceCount);
                for (rapidjson::SizeType i = 0; i < influenceCount; ++i)
                {
                    const float influence = influences[i].IsNumber() ? influences[i].GetFloat() : 1.0f;
                    state.jointInfluences.push_back(std::isfinite(influence) ? std::clamp(influence, 0.0f, 2.0f) : 1.0f);
                }
            }

            state.widthScales.clear();
            if (sv.HasMember("width_scales") && sv["width_scales"].IsArray())
            {
                const auto &widthScales = sv["width_scales"];
                rapidjson::SizeType widthScaleCount = widthScales.Size();
                const int jointCount = scene.GetJointCountForNode(node);
                if (jointCount > 0)
                    widthScaleCount = std::min(widthScaleCount, static_cast<rapidjson::SizeType>(jointCount));

                state.widthScales.reserve(widthScaleCount);
                for (rapidjson::SizeType i = 0; i < widthScaleCount; ++i)
                {
                    const float widthScale = widthScales[i].IsNumber() ? widthScales[i].GetFloat() : 1.0f;
                    state.widthScales.push_back(std::isfinite(widthScale) ? std::clamp(widthScale, 0.05f, 3.0f) : 1.0f);
                }
            }

            ApplySceneSkinnedStrip2DPose(scene, node);
        }

#ifdef PE_PHYSICS2D
        void WritePhysics2DDesc(rapidjson::Value &phys, const Physics2DBodyDesc &desc, rapidjson::Document::AllocatorType &allocator)
        {
            phys.AddMember("body_type", static_cast<int>(desc.bodyType), allocator);
            phys.AddMember("shape_type", static_cast<int>(desc.shapeType), allocator);
            phys.AddMember("width", desc.width, allocator);
            phys.AddMember("height", desc.height, allocator);
            phys.AddMember("radius", desc.radius, allocator);
            phys.AddMember("capsule_height", desc.capsuleHeight, allocator);
            phys.AddMember("capsule_radius", desc.capsuleRadius, allocator);
            phys.AddMember("density", desc.density, allocator);
            phys.AddMember("friction", desc.friction, allocator);
            phys.AddMember("restitution", desc.restitution, allocator);
            phys.AddMember("linear_damping", desc.linearDamping, allocator);
            phys.AddMember("angular_damping", desc.angularDamping, allocator);
            phys.AddMember("gravity_scale", desc.gravityScale, allocator);

            rapidjson::Value category;
            category.SetUint64(desc.categoryBits);
            phys.AddMember("category_bits", category, allocator);

            rapidjson::Value mask;
            mask.SetUint64(desc.maskBits);
            phys.AddMember("mask_bits", mask, allocator);

            phys.AddMember("group_index", desc.groupIndex, allocator);
            phys.AddMember("fixed_rotation", desc.fixedRotation, allocator);
            phys.AddMember("is_sensor", desc.isSensor, allocator);
            phys.AddMember("sync_node", desc.syncNode, allocator);
            phys.AddMember("bullet", desc.bullet, allocator);
            phys.AddMember("enable_sleep", desc.enableSleep, allocator);
        }

        Physics2DBodyDesc ReadPhysics2DDesc(const rapidjson::Value &pv)
        {
            Physics2DBodyDesc desc;
            if (!pv.IsObject())
                return desc;

            auto readUint64 = [](const rapidjson::Value &value, uint64_t fallback) -> uint64_t
            {
                if (value.IsUint64())
                    return value.GetUint64();
                if (value.IsUint())
                    return value.GetUint();
                if (value.IsInt() && value.GetInt() >= 0)
                    return static_cast<uint64_t>(value.GetInt());
                return fallback;
            };

            if (pv.HasMember("body_type") && pv["body_type"].IsInt())
                desc.bodyType = static_cast<Physics2DBodyType>(std::clamp(pv["body_type"].GetInt(), 0, 2));
            if (pv.HasMember("shape_type") && pv["shape_type"].IsInt())
                desc.shapeType = static_cast<Physics2DShapeType>(std::clamp(pv["shape_type"].GetInt(), 0, 2));
            if (pv.HasMember("width") && pv["width"].IsNumber())
                desc.width = pv["width"].GetFloat();
            if (pv.HasMember("height") && pv["height"].IsNumber())
                desc.height = pv["height"].GetFloat();
            if (pv.HasMember("radius") && pv["radius"].IsNumber())
                desc.radius = pv["radius"].GetFloat();
            if (pv.HasMember("capsule_height") && pv["capsule_height"].IsNumber())
                desc.capsuleHeight = pv["capsule_height"].GetFloat();
            if (pv.HasMember("capsule_radius") && pv["capsule_radius"].IsNumber())
                desc.capsuleRadius = pv["capsule_radius"].GetFloat();
            if (pv.HasMember("density") && pv["density"].IsNumber())
                desc.density = pv["density"].GetFloat();
            if (pv.HasMember("friction") && pv["friction"].IsNumber())
                desc.friction = pv["friction"].GetFloat();
            if (pv.HasMember("restitution") && pv["restitution"].IsNumber())
                desc.restitution = pv["restitution"].GetFloat();
            if (pv.HasMember("linear_damping") && pv["linear_damping"].IsNumber())
                desc.linearDamping = pv["linear_damping"].GetFloat();
            if (pv.HasMember("angular_damping") && pv["angular_damping"].IsNumber())
                desc.angularDamping = pv["angular_damping"].GetFloat();
            if (pv.HasMember("gravity_scale") && pv["gravity_scale"].IsNumber())
                desc.gravityScale = pv["gravity_scale"].GetFloat();
            if (pv.HasMember("category_bits"))
                desc.categoryBits = readUint64(pv["category_bits"], desc.categoryBits);
            if (pv.HasMember("mask_bits"))
                desc.maskBits = readUint64(pv["mask_bits"], desc.maskBits);
            if (pv.HasMember("group_index") && pv["group_index"].IsInt())
                desc.groupIndex = pv["group_index"].GetInt();
            if (pv.HasMember("fixed_rotation") && pv["fixed_rotation"].IsBool())
                desc.fixedRotation = pv["fixed_rotation"].GetBool();
            if (pv.HasMember("is_sensor") && pv["is_sensor"].IsBool())
                desc.isSensor = pv["is_sensor"].GetBool();
            if (pv.HasMember("sync_node") && pv["sync_node"].IsBool())
                desc.syncNode = pv["sync_node"].GetBool();
            if (pv.HasMember("bullet") && pv["bullet"].IsBool())
                desc.bullet = pv["bullet"].GetBool();
            if (pv.HasMember("enable_sleep") && pv["enable_sleep"].IsBool())
                desc.enableSleep = pv["enable_sleep"].GetBool();
            return desc;
        }

        void RestorePhysics2DNode(Scene &scene, NodeId *node, const rapidjson::Value &nodeValue)
        {
            if (!node || !nodeValue.HasMember("physics2d") || !nodeValue["physics2d"].IsObject())
                return;
            AddScenePhysics2DBody(scene, node, ReadPhysics2DDesc(nodeValue["physics2d"]));
        }
#endif

        std::filesystem::path ResolveSerializedTexturePath(const rapidjson::Value &textureValue,
                                                           const std::filesystem::path *relativeToDir)
        {
            return ResolveSerializedFilePath(textureValue, relativeToDir);
        }

        std::filesystem::path ResolveSerializedFilePath(const rapidjson::Value &pathValue,
                                                        const std::filesystem::path *relativeToDir)
        {
            if (!pathValue.IsString() || pathValue.GetStringLength() == 0)
                return {};

            std::filesystem::path path = pathValue.GetString();
            if (!path.is_relative())
                return path.lexically_normal();

            if (relativeToDir)
                return (*relativeToDir / path).lexically_normal();

            const std::filesystem::path assetsPath = std::filesystem::path(Path::Assets) / path;
            if (std::filesystem::exists(assetsPath))
                return assetsPath.lexically_normal();

            return path.lexically_normal();
        }

        void ResetMaterialTexturesToDefaults(Material &material)
        {
            const auto &defaults = ModelAsset::GetDefaultResources();
            material.textures[static_cast<int>(TextureType::BaseColor)] = ResourceHandle<Image>::FromRaw(defaults.white);
            material.textures[static_cast<int>(TextureType::MetallicRoughness)] = ResourceHandle<Image>::FromRaw(defaults.white);
            material.textures[static_cast<int>(TextureType::Normal)] = ResourceHandle<Image>::FromRaw(defaults.normal);
            material.textures[static_cast<int>(TextureType::Occlusion)] = ResourceHandle<Image>::FromRaw(defaults.white);
            material.textures[static_cast<int>(TextureType::Emissive)] = ResourceHandle<Image>::FromRaw(defaults.black);
        }

        void RestoreMaterialTexturesFromJson(CommandBuffer *cmd,
                                             Material &material,
                                             const rapidjson::Value &meshValue,
                                             const std::filesystem::path *relativeToDir)
        {
            ResetMaterialTexturesToDefaults(material);
            if (!cmd || !meshValue.HasMember("textures") || !meshValue["textures"].IsObject())
                return;

            const auto &texVal = meshValue["textures"];
            const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
            ModelAsset textureLoader;
            for (int k = 0; k < 5; k++)
            {
                if (!texVal.HasMember(texSlotNames[k]))
                    continue;

                const std::filesystem::path texPath = ResolveSerializedTexturePath(texVal[texSlotNames[k]], relativeToDir);
                if (texPath.empty() || !std::filesystem::exists(texPath))
                    continue;

                ResourceHandle<Image> img = textureLoader.LoadTexture(cmd, texPath);
                if (img)
                    material.textures[k] = img;
            }
        }
    } // namespace

    struct SceneSerializationHelper
    {
        enum class LegacyCameraMode
        {
            SaveFile,
            Snapshot
        };

        const Scene &scene;
        rapidjson::Document &document;
        rapidjson::Document::AllocatorType &allocator;
        std::vector<int> meshRemap;
        std::vector<int> srcRemap;
        std::vector<NodeId *> nodeSubset;
        std::vector<int> nodeRemap;
        NodeId *skipPrefabPathNode = nullptr;

        SceneSerializationHelper(const Scene &scene, rapidjson::Document &document)
            : scene(scene), document(document), allocator(document.GetAllocator()),
              meshRemap(scene.m_meshes.size(), -1), srcRemap(scene.m_sources.size(), -1)
        {
        }

        static float SafeFloat(float f)
        {
            return std::isnan(f) || std::isinf(f) ? 0.0f : f;
        }

        void SetVec3(rapidjson::Value &arr, const vec3 &v) const
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator);
        }

        void SetVec2(rapidjson::Value &arr, const vec2 &v) const
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator);
        }

        void SetVec4(rapidjson::Value &arr, const vec4 &v) const
        {
            arr.SetArray();
            arr.PushBack(SafeFloat(v.x), allocator).PushBack(SafeFloat(v.y), allocator).PushBack(SafeFloat(v.z), allocator).PushBack(SafeFloat(v.w), allocator);
        }

        void SetMat4(rapidjson::Value &arr, const mat4 &m) const
        {
            arr.SetArray();
            const float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                arr.PushBack(SafeFloat(p[i]), allocator);
        }

        rapidjson::Value MakeStringValue(const std::string &value) const
        {
            rapidjson::Value stringValue;
            stringValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.length()), allocator);
            return stringValue;
        }

        static std::string NormalizeSeparators(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        static std::string PathToUtf8String(const std::filesystem::path &path)
        {
            auto u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string SerializePath(const std::filesystem::path &path, const std::filesystem::path *relativeToDir) const
        {
            if (relativeToDir)
                return NormalizeSeparators(PathToUtf8String(std::filesystem::relative(path, *relativeToDir)));

            return NormalizeSeparators(PathToUtf8String(path));
        }

        std::string SerializeTexturePath(const std::string &path, const std::filesystem::path *relativeToDir) const
        {
            if (path.empty())
                return {};

            std::filesystem::path texturePath(path);
            if (relativeToDir)
                return SerializePath(texturePath, relativeToDir);

            return NormalizeSeparators(path);
        }

        void BuildLiveGeometryRemap()
        {
            std::vector<bool> liveMesh(scene.m_meshes.size(), false);
            for (uint32_t ni = 0; ni < scene.GetNodeCount(); ni++)
            {
                for (int meshRef : scene.m_nodeComponentCache[ni].meshRefs->meshRefs)
                {
                    if (meshRef >= 0 && meshRef < static_cast<int>(scene.m_meshes.size()))
                        liveMesh[meshRef] = true;
                }
            }

            int newMeshIdx = 0;
            for (int mi = 0; mi < static_cast<int>(scene.m_meshes.size()); mi++)
            {
                if (liveMesh[mi])
                    meshRemap[mi] = newMeshIdx++;
            }

            std::vector<bool> liveSrc(scene.m_sources.size(), false);
            for (int mi = 0; mi < static_cast<int>(scene.m_meshes.size()); mi++)
            {
                if (meshRemap[mi] < 0)
                    continue;

                if (mi < static_cast<int>(scene.m_meshSourceInfos.size()))
                {
                    int sourceIndex = scene.m_meshSourceInfos[mi].sourceIndex;
                    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(scene.m_sources.size()))
                        liveSrc[sourceIndex] = true;
                }
            }

            int newSrcIdx = 0;
            for (int si = 0; si < static_cast<int>(scene.m_sources.size()); si++)
            {
                if (liveSrc[si])
                    srcRemap[si] = newSrcIdx++;
            }
        }

        bool BuildSubtreeGeometryRemap(NodeId *root)
        {
            if (!scene.IsNodeAlive(root))
                return false;

            nodeSubset.clear();
            nodeRemap.assign(scene.GetNodeCount(), -1);

            auto collect = [&](auto &&self, NodeId *node) -> void
            {
                if (!scene.IsNodeAlive(node))
                    return;

                nodeRemap[node->index] = static_cast<int>(nodeSubset.size());
                nodeSubset.push_back(node);
                for (NodeId *child : scene.GetChildren(node))
                    self(self, child);
            };
            collect(collect, root);

            std::vector<bool> liveMesh(scene.m_meshes.size(), false);
            for (NodeId *node : nodeSubset)
            {
                for (int meshRef : scene.m_nodeComponentCache[node->index].meshRefs->meshRefs)
                {
                    if (meshRef >= 0 && meshRef < static_cast<int>(scene.m_meshes.size()))
                        liveMesh[meshRef] = true;
                }
            }

            int newMeshIdx = 0;
            for (int mi = 0; mi < static_cast<int>(scene.m_meshes.size()); mi++)
            {
                if (liveMesh[mi])
                    meshRemap[mi] = newMeshIdx++;
            }

            std::vector<bool> liveSrc(scene.m_sources.size(), false);
            for (int mi = 0; mi < static_cast<int>(scene.m_meshes.size()); mi++)
            {
                if (meshRemap[mi] < 0 || mi >= static_cast<int>(scene.m_meshSourceInfos.size()))
                    continue;

                int sourceIndex = scene.m_meshSourceInfos[mi].sourceIndex;
                if (sourceIndex >= 0 && sourceIndex < static_cast<int>(scene.m_sources.size()))
                    liveSrc[sourceIndex] = true;
            }

            int newSrcIdx = 0;
            for (int si = 0; si < static_cast<int>(scene.m_sources.size()); si++)
            {
                if (liveSrc[si])
                    srcRemap[si] = newSrcIdx++;
            }

            return true;
        }

        void AddSettings()
        {
            rapidjson::Value settings(rapidjson::kObjectType);
            AddSceneSettingsMembers(settings, allocator);

            document.AddMember("settings", settings.Move(), allocator);
        }

        // Scene-owned script references (on_play scripts + named actions). Paths are stored
        // verbatim (already project-relative as authored), so no source remapping is needed.
        void AddSceneScripts()
        {
            const SceneScriptManifest &manifest = scene.m_scriptManifest;
            if (manifest.Empty())
                return;

            rapidjson::Value obj(rapidjson::kObjectType);

            if (!manifest.onPlay.empty())
            {
                rapidjson::Value arr(rapidjson::kArrayType);
                for (const std::string &path : manifest.onPlay)
                    arr.PushBack(MakeStringValue(path), allocator);
                obj.AddMember("on_play", arr.Move(), allocator);
            }

            if (!manifest.actions.empty())
            {
                rapidjson::Value actions(rapidjson::kObjectType);
                for (const SceneScriptAction &a : manifest.actions)
                {
                    if (a.id.empty())
                        continue;
                    rapidjson::Value entry(rapidjson::kObjectType);
                    entry.AddMember("script", MakeStringValue(a.script), allocator);
                    entry.AddMember("function", MakeStringValue(a.function), allocator);
                    actions.AddMember(MakeStringValue(a.id), entry.Move(), allocator);
                }
                obj.AddMember("actions", actions.Move(), allocator);
            }

            document.AddMember("scene_scripts", obj.Move(), allocator);
        }

        void AddSources(const std::filesystem::path *relativeToDir)
        {
            rapidjson::Value sourcesArr(rapidjson::kArrayType);
            for (int si = 0; si < static_cast<int>(scene.m_sources.size()); si++)
            {
                if (srcRemap[si] < 0)
                    continue;

                const auto &src = scene.m_sources[si];
                rapidjson::Value srcObj(rapidjson::kObjectType);
                if (!src.primitiveType.empty())
                {
                    srcObj.AddMember("primitive_type", MakeStringValue(src.primitiveType), allocator);
                    WritePrimitiveParams(srcObj, src.primitiveParams, src.primitiveParamCount, allocator);
                }
                else
                {
                    srcObj.AddMember("path", MakeStringValue(SerializePath(src.filePath, relativeToDir)), allocator);
                }
                sourcesArr.PushBack(srcObj.Move(), allocator);
            }

            document.AddMember("sources", sourcesArr.Move(), allocator);
        }

        void AddMeshes(const std::filesystem::path *relativeToDir)
        {
            rapidjson::Value meshesArr(rapidjson::kArrayType);
            const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
            const auto &defaults = ModelAsset::GetDefaultResources();

            for (int mi = 0; mi < static_cast<int>(scene.m_meshes.size()); mi++)
            {
                if (meshRemap[mi] < 0)
                    continue;

                const Mesh &mesh = scene.m_meshes[mi];
                rapidjson::Value meshObj(rapidjson::kObjectType);

                if (mi < static_cast<int>(scene.m_meshSourceInfos.size()))
                {
                    int rawSrc = scene.m_meshSourceInfos[mi].sourceIndex;
                    int remappedSrc = (rawSrc >= 0 && rawSrc < static_cast<int>(srcRemap.size())) ? srcRemap[rawSrc] : -1;
                    meshObj.AddMember("source", remappedSrc, allocator);
                    meshObj.AddMember("source_mesh", scene.m_meshSourceInfos[mi].sourceMeshIndex, allocator);
                }

                meshObj.AddMember("render_type", static_cast<int>(mesh.renderType), allocator);
                if (mesh.lodShift != 0)
                    meshObj.AddMember("lod_shift", mesh.lodShift, allocator);
                if (!mesh.lodEnabled)
                    meshObj.AddMember("lod_enabled", mesh.lodEnabled, allocator);
                if (mesh.lodBias != 1.0f)
                    meshObj.AddMember("lod_bias", mesh.lodBias, allocator);

                Material resolvedMat;
                const Material *saveMat = nullptr;
                if (mesh.materialInstance)
                {
                    resolvedMat = mesh.materialInstance->Resolve();
                    saveMat = &resolvedMat;
                    meshObj.AddMember("instanced", true, allocator);
                }
                else if (mesh.material)
                {
                    saveMat = mesh.material;
                }

                meshObj.AddMember("texture_mask", saveMat ? saveMat->textureMask : 0u, allocator);
                meshObj.AddMember("double_sided", saveMat ? saveMat->doubleSided : false, allocator);

                mat4 factors[2] = {mat4(1.f), mat4(1.f)};
                if (saveMat)
                    saveMat->PackLegacyFactors(factors);
                EncodeMaterialFactorsForPersistence(mesh.renderType, factors[0], factors[1]);

                rapidjson::Value factorsArr(rapidjson::kArrayType);
                rapidjson::Value f0;
                rapidjson::Value f1;
                SetMat4(f0, factors[0]);
                SetMat4(f1, factors[1]);
                factorsArr.PushBack(f0.Move(), allocator);
                factorsArr.PushBack(f1.Move(), allocator);
                meshObj.AddMember("material_factors", factorsArr.Move(), allocator);

                rapidjson::Value texturesObj(rapidjson::kObjectType);
                for (int i = 0; i < 5; i++)
                {
                    Image *img = saveMat ? saveMat->textures[i].get() : nullptr;
                    if (img && !img->GetName().empty() &&
                        img != defaults.black && img != defaults.white && img != defaults.normal)
                    {
                        const std::string texName = SerializeTexturePath(img->GetName(), relativeToDir);
                        if (!texName.empty())
                        {
                            texturesObj.AddMember(
                                rapidjson::Value(texSlotNames[i], allocator).Move(),
                                MakeStringValue(texName),
                                allocator);
                        }
                    }
                }
                meshObj.AddMember("textures", texturesObj.Move(), allocator);
                meshesArr.PushBack(meshObj.Move(), allocator);
            }

            document.AddMember("meshes", meshesArr.Move(), allocator);
        }

        void AddNodes(const std::filesystem::path *relativeToDir = nullptr)
        {
            rapidjson::Value nodesArr(rapidjson::kArrayType);
            const bool scopedNodes = !nodeSubset.empty();
            const uint32_t nodeCount = scopedNodes ? static_cast<uint32_t>(nodeSubset.size()) : scene.GetNodeCount();
            for (uint32_t ni = 0; ni < nodeCount; ni++)
            {
                NodeId *node = scopedNodes ? nodeSubset[ni] : scene.m_nodeIds[ni];
                rapidjson::Value nodeObj(rapidjson::kObjectType);
                const auto &cache = scene.m_nodeComponentCache[node->index];
                nodeObj.AddMember("name", MakeStringValue(cache.name->name), allocator);

                NodeId *parentNode = cache.hierarchy->parent;
                int parentIdx = -1;
                if (parentNode)
                {
                    if (scopedNodes)
                    {
                        parentIdx = parentNode->index < nodeRemap.size() ? nodeRemap[parentNode->index] : -1;
                    }
                    else
                    {
                        parentIdx = static_cast<int>(parentNode->index);
                    }
                }
                nodeObj.AddMember("parent", parentIdx, allocator);
                if (!cache.hierarchy->enabled)
                    nodeObj.AddMember("enabled", false, allocator);

                rapidjson::Value localMat;
                SetMat4(localMat, cache.transform->localMatrix);
                nodeObj.AddMember("local_matrix", localMat.Move(), allocator);

                // Mesh refs (single as "mesh" for compat, multi as "mesh_refs")
                const auto &meshRefsList = cache.meshRefs->meshRefs;
                if (meshRefsList.size() == 1)
                {
                    int remapped = (meshRefsList[0] >= 0 && meshRefsList[0] < static_cast<int>(meshRemap.size())) ? meshRemap[meshRefsList[0]] : -1;
                    if (remapped >= 0)
                        nodeObj.AddMember("mesh", remapped, allocator);
                }
                else if (meshRefsList.size() > 1)
                {
                    rapidjson::Value meshArr(rapidjson::kArrayType);
                    for (int mr : meshRefsList)
                    {
                        int remapped = (mr >= 0 && mr < static_cast<int>(meshRemap.size())) ? meshRemap[mr] : -1;
                        if (remapped >= 0)
                            meshArr.PushBack(remapped, allocator);
                    }
                    if (!meshArr.Empty())
                        nodeObj.AddMember("mesh_refs", meshArr.Move(), allocator);
                }

                if (!cache.script->path.empty())
                {
                    nodeObj.AddMember("script", MakeStringValue(cache.script->path), allocator);
                    nodeObj.AddMember("scriptRunMode", static_cast<int>(cache.script->runMode), allocator);
                }

                if (node != skipPrefabPathNode && cache.prefab && !cache.prefab->path.empty())
                    nodeObj.AddMember("prefab", MakeStringValue(SerializePath(cache.prefab->path, relativeToDir)), allocator);

                uint32_t flags = scene.GetComponentFlags(node) & ~Component_GpuPending;
                if (flags)
                    nodeObj.AddMember("component_flags", flags, allocator);

                if ((flags & Component_Camera) && !scene.m_cameras.empty())
                {
                    Camera *cam = scene.GetCameraForNode(node);
                    if (cam)
                    {
                        rapidjson::Value camObj(rapidjson::kObjectType);
                        camObj.AddMember("projection", MakeStringValue(cam->IsOrthographic() ? "orthographic" : "perspective"), allocator);
                        camObj.AddMember("fovx", cam->Fovx(), allocator);
                        camObj.AddMember("orthographic_size", cam->GetOrthographicSize(), allocator);
                        camObj.AddMember("near_plane", cam->GetNearPlane(), allocator);
                        camObj.AddMember("far_plane", cam->GetFarPlane(), allocator);
                        camObj.AddMember("speed", cam->GetSpeed(), allocator);
                        rapidjson::Value eul;
                        SetVec3(eul, cam->GetEuler());
                        camObj.AddMember("euler", eul.Move(), allocator);
                        nodeObj.AddMember("camera", camObj.Move(), allocator);
                    }
                }

                if (flags & Component_Light)
                {
                    auto [lightType, lightIndex] = scene.GetLightForNode(node);
                    if (lightIndex >= 0)
                    {
                        rapidjson::Value lightObj(rapidjson::kObjectType);
                        if (lightType == LightType::Directional)
                        {
                            const DirectionalLight &light = scene.m_directionalLights[lightIndex];
                            lightObj.AddMember("type", "directional", allocator);
                            rapidjson::Value color;
                            SetVec4(color, light.color);
                            lightObj.AddMember("color", color.Move(), allocator);
                        }
                        else if (lightType == LightType::Point)
                        {
                            const PointLight &light = scene.m_pointLights[lightIndex];
                            lightObj.AddMember("type", "point", allocator);
                            rapidjson::Value color;
                            SetVec4(color, light.color);
                            lightObj.AddMember("color", color.Move(), allocator);
                            lightObj.AddMember("radius", light.position.w, allocator);
                        }
                        else if (lightType == LightType::Spot)
                        {
                            const SpotLight &light = scene.m_spotLights[lightIndex];
                            lightObj.AddMember("type", "spot", allocator);
                            rapidjson::Value color;
                            rapidjson::Value params;
                            SetVec4(color, light.color);
                            SetVec4(params, light.params);
                            lightObj.AddMember("color", color.Move(), allocator);
                            lightObj.AddMember("range", light.position.w, allocator);
                            lightObj.AddMember("params", params.Move(), allocator);
                        }
                        else if (lightType == LightType::Area)
                        {
                            const AreaLight &light = scene.m_areaLights[lightIndex];
                            lightObj.AddMember("type", "area", allocator);
                            rapidjson::Value color;
                            rapidjson::Value size;
                            SetVec4(color, light.color);
                            SetVec4(size, light.size);
                            lightObj.AddMember("color", color.Move(), allocator);
                            lightObj.AddMember("range", light.position.w, allocator);
                            lightObj.AddMember("size", size.Move(), allocator);
                        }
                        nodeObj.AddMember("light", lightObj.Move(), allocator);
                    }
                }

                if (flags & Component_Physics)
                {
                    const PhysicsBodyDesc *desc = GetScenePhysicsBodyDesc(node);
                    if (desc)
                    {
                        rapidjson::Value phys(rapidjson::kObjectType);
                        phys.AddMember("body_type", static_cast<int>(desc->bodyType), allocator);
                        phys.AddMember("shape_type", static_cast<int>(desc->shapeType), allocator);
                        phys.AddMember("mass", desc->mass, allocator);
                        phys.AddMember("friction", desc->friction, allocator);
                        phys.AddMember("restitution", desc->restitution, allocator);
                        phys.AddMember("auto_fit", desc->autoFitShape, allocator);
                        phys.AddMember("is_trigger", desc->isTrigger, allocator);
                        rapidjson::Value halfExtents;
                        SetVec3(halfExtents, desc->boxHalfExtents);
                        phys.AddMember("box_half_extents", halfExtents.Move(), allocator);
                        phys.AddMember("sphere_radius", desc->sphereRadius, allocator);
                        phys.AddMember("capsule_half_height", desc->capsuleHalfHeight, allocator);
                        phys.AddMember("capsule_radius", desc->capsuleRadius, allocator);
                        nodeObj.AddMember("physics", phys.Move(), allocator);
                    }
                }

#ifdef PE_PHYSICS2D
                if (flags & Component_Physics2D)
                {
                    const Physics2DBodyDesc *desc = GetScenePhysics2DBodyDesc(node);
                    if (desc)
                    {
                        rapidjson::Value phys2d(rapidjson::kObjectType);
                        WritePhysics2DDesc(phys2d, *desc, allocator);
                        nodeObj.AddMember("physics2d", phys2d.Move(), allocator);
                    }
                }
#endif

                if (flags & Component_Audio)
                {
                    const AudioSourceDesc *desc = GetSceneAudioSourceDesc(node);
                    if (desc)
                    {
                        rapidjson::Value audio(rapidjson::kObjectType);
                        audio.AddMember("file", MakeStringValue(desc->filePath), allocator);
                        audio.AddMember("volume", desc->volume, allocator);
                        audio.AddMember("pitch", desc->pitch, allocator);
                        audio.AddMember("min_distance", desc->minDistance, allocator);
                        audio.AddMember("max_distance", desc->maxDistance, allocator);
                        audio.AddMember("loop", desc->loop, allocator);
                        audio.AddMember("spatial", desc->spatial, allocator);
                        audio.AddMember("autoplay", desc->autoplay, allocator);
                        nodeObj.AddMember("audio", audio.Move(), allocator);
                    }
                }

                if ((flags & Component_Skybox) && cache.skybox)
                {
                    const NodeSkyboxTag &skybox = *cache.skybox;
                    rapidjson::Value skyboxObj(rapidjson::kObjectType);
                    skyboxObj.AddMember("path", MakeStringValue(skybox.path), allocator);
                    nodeObj.AddMember("skybox", skyboxObj.Move(), allocator);
                }

                if ((flags & Component_TriggerZone) && cache.triggerZone)
                {
                    const NodeTriggerZoneTag &z = *cache.triggerZone;
                    rapidjson::Value zObj(rapidjson::kObjectType);
                    // common
                    zObj.AddMember("priority", z.priority, allocator);
                    zObj.AddMember("blend", z.blend, allocator);
                    zObj.AddMember("blend_distance", z.blend_distance, allocator);
                    zObj.AddMember("runMode", static_cast<int>(z.runMode), allocator);
                    zObj.AddMember("shape", static_cast<int>(z.shape), allocator);
                    // script section
                    zObj.AddMember("scriptEnabled", z.scriptEnabled, allocator);
                    zObj.AddMember("scriptPath", rapidjson::Value(z.scriptPath.c_str(), allocator), allocator);
                    zObj.AddMember("fireForCamera", z.fireForCamera, allocator);
                    zObj.AddMember("onEnter", rapidjson::Value(z.onEnter.c_str(), allocator), allocator);
                    zObj.AddMember("onExit", rapidjson::Value(z.onExit.c_str(), allocator), allocator);
                    // post-process section
                    zObj.AddMember("postProcessEnabled", z.postProcessEnabled, allocator);
                    rapidjson::Value profileObj(rapidjson::kObjectType);
                    AddPostProcessProfileMembers(profileObj, allocator, z.postProcess);
                    zObj.AddMember("postProcess", profileObj.Move(), allocator);
                    // audio section (zone-owned source)
                    zObj.AddMember("audioEnabled", z.audioEnabled, allocator);
                    {
                        rapidjson::Value a(rapidjson::kObjectType);
                        a.AddMember("file", MakeStringValue(z.audioSource.filePath), allocator);
                        a.AddMember("volume", z.audioSource.volume, allocator);
                        a.AddMember("pitch", z.audioSource.pitch, allocator);
                        a.AddMember("min_distance", z.audioSource.minDistance, allocator);
                        a.AddMember("max_distance", z.audioSource.maxDistance, allocator);
                        a.AddMember("loop", z.audioSource.loop, allocator);
                        a.AddMember("spatial", z.audioSource.spatial, allocator);
                        zObj.AddMember("audioSource", a.Move(), allocator);
                    }
                    // physics section
                    zObj.AddMember("physicsEnabled", z.physicsEnabled, allocator);
                    zObj.AddMember("physicsEngine", static_cast<int>(z.physicsEngine), allocator);
                    zObj.AddMember("physicsMode", static_cast<int>(z.physicsMode), allocator);
                    zObj.AddMember("physicsBodyType", static_cast<int>(z.physicsBodyType), allocator);
                    zObj.AddMember("physicsMass", z.physicsMass, allocator);
                    zObj.AddMember("physicsFriction", z.physicsFriction, allocator);
                    zObj.AddMember("physicsRestitution", z.physicsRestitution, allocator);
                    zObj.AddMember("physicsFilterTag", MakeStringValue(z.physicsFilterTag), allocator);
                    zObj.AddMember("physicsScriptPath", MakeStringValue(z.physicsScriptPath), allocator);
                    zObj.AddMember("physicsOnEnter", rapidjson::Value(z.physicsOnEnter.c_str(), allocator), allocator);
                    zObj.AddMember("physicsOnExit", rapidjson::Value(z.physicsOnExit.c_str(), allocator), allocator);
                    zObj.AddMember("physicsForceField", z.physicsForceField, allocator);
                    zObj.AddMember("physicsForceX", z.physicsForce.x, allocator);
                    zObj.AddMember("physicsForceY", z.physicsForce.y, allocator);
                    zObj.AddMember("physicsForceZ", z.physicsForce.z, allocator);
                    // spawn section
                    zObj.AddMember("spawnEnabled", z.spawnEnabled, allocator);
                    zObj.AddMember("spawnPrefabPath", MakeStringValue(z.spawnPrefabPath), allocator);
                    zObj.AddMember("spawnDespawnOnExit", z.spawnDespawnOnExit, allocator);
                    // streaming section
                    zObj.AddMember("streamEnabled", z.streamEnabled, allocator);
                    zObj.AddMember("streamTargetName", MakeStringValue(z.streamTargetName), allocator);
                    // camera section
                    zObj.AddMember("cameraEnabled", z.cameraEnabled, allocator);
                    zObj.AddMember("cameraTargetName", MakeStringValue(z.cameraTargetName), allocator);
                    zObj.AddMember("cameraFovDeg", z.cameraFovDeg, allocator);
                    nodeObj.AddMember("triggerZone", zObj.Move(), allocator);
                }

                if ((flags & Component_VoxelWorld) && cache.voxelWorld)
                {
                    const NodeVoxelWorldTag &v = *cache.voxelWorld;
                    rapidjson::Value vObj(rapidjson::kObjectType);
                    vObj.AddMember("enabled", v.worldEnabled, allocator);
                    vObj.AddMember("streaming", v.streaming, allocator);
                    vObj.AddMember("anchorFollowsCamera", v.anchorFollowsCamera, allocator);
                    vObj.AddMember("loadRadius", v.loadRadius, allocator);
                    vObj.AddMember("unloadMargin", v.unloadMargin, allocator);
                    vObj.AddMember("uploadBudget", v.uploadBudget, allocator);
                    vObj.AddMember("groundY", v.groundY, allocator);
                    vObj.AddMember("worldRadius", v.worldRadius, allocator);
                    vObj.AddMember("lodEnabled", v.lodEnabled, allocator);
                    vObj.AddMember("lod0Radius", v.lod0Radius, allocator);
                    vObj.AddMember("saveDir", MakeStringValue(v.saveDir), allocator);
                    vObj.AddMember("noiseAmplitude", v.noiseAmplitude, allocator);
                    vObj.AddMember("noiseFeatureScale", v.noiseFeatureScale, allocator);
                    vObj.AddMember("noiseSeed", v.noiseSeed, allocator);
                    vObj.AddMember("caves", v.caves, allocator);
                    vObj.AddMember("seaLevel", v.seaLevel, allocator);
                    vObj.AddMember("heightmapPath", MakeStringValue(v.heightmapPath), allocator);
                    vObj.AddMember("strata1Path", MakeStringValue(v.strata1Path), allocator);
                    vObj.AddMember("strata2Path", MakeStringValue(v.strata2Path), allocator);
                    vObj.AddMember("featuresPath", MakeStringValue(v.featuresPath), allocator);
                    vObj.AddMember("blocksPerPixel", v.blocksPerPixel, allocator);
                    vObj.AddMember("heightMin", v.heightMin, allocator);
                    vObj.AddMember("heightMax", v.heightMax, allocator);
                    vObj.AddMember("groundHeight", v.groundHeight, allocator);
                    vObj.AddMember("seaLevelM", v.seaLevelM, allocator);
                    vObj.AddMember("smooth", v.smooth, allocator);
                    vObj.AddMember("physics", v.physics, allocator);
                    vObj.AddMember("physicsFriction", v.physicsFriction, allocator);
                    vObj.AddMember("physicsRestitution", v.physicsRestitution, allocator);
                    vObj.AddMember("autoRebuild", v.autoRebuild, allocator);
                    vObj.AddMember("surfaceBlock", v.surfaceBlock, allocator);
                    vObj.AddMember("surfaceBands", v.surfaceBands, allocator);
                    vObj.AddMember("strata1Block", v.strata1Block, allocator);
                    vObj.AddMember("strata2Block", v.strata2Block, allocator);
                    vObj.AddMember("fillBlock", v.fillBlock, allocator);
                    vObj.AddMember("strata1Thickness", v.strata1Thickness, allocator);
                    vObj.AddMember("strata2Thickness", v.strata2Thickness, allocator);
                    nodeObj.AddMember("voxelWorld", vObj.Move(), allocator);
                }

                if ((flags & Component_RuntimeUi) && cache.runtimeUi && cache.runtimeUi->authored)
                {
                    const NodeRuntimeUiTag &ui = *cache.runtimeUi;
                    rapidjson::Value uiObj(rapidjson::kObjectType);
                    uiObj.AddMember("type", MakeStringValue(RuntimeUiWidgetTypeToString(ui.widgetType)), allocator);
                    if (!ui.screenId.empty())
                        uiObj.AddMember("screen", MakeStringValue(ui.screenId), allocator);
                    if (!ui.widgetId.empty())
                        uiObj.AddMember("id", MakeStringValue(ui.widgetId), allocator);
                    if (!ui.label.empty())
                        uiObj.AddMember("label", MakeStringValue(ui.label), allocator);
                    if (!ui.title.empty())
                        uiObj.AddMember("title", MakeStringValue(ui.title), allocator);
                    if (!ui.subtitle.empty())
                        uiObj.AddMember("subtitle", MakeStringValue(ui.subtitle), allocator);
                    if (!ui.body.empty())
                        uiObj.AddMember("body", MakeStringValue(ui.body), allocator);
                    if (!ui.footer.empty())
                        uiObj.AddMember("footer", MakeStringValue(ui.footer), allocator);
                    if (!ui.imagePath.empty())
                        uiObj.AddMember("image", MakeStringValue(SerializePath(ui.imagePath, relativeToDir)), allocator);
                    if (!ui.actionName.empty())
                        uiObj.AddMember("action", MakeStringValue(ui.actionName), allocator);
                    if (!ui.actionFunction.empty())
                        uiObj.AddMember("action_function", MakeStringValue(ui.actionFunction), allocator);

                    rapidjson::Value fill;
                    SetVec4(fill, ui.fillColor);
                    uiObj.AddMember("fill", fill.Move(), allocator);

                    rapidjson::Value border;
                    SetVec4(border, ui.borderColor);
                    uiObj.AddMember("border", border.Move(), allocator);

                    rapidjson::Value accent;
                    SetVec4(accent, ui.accentColor);
                    uiObj.AddMember("accent", accent.Move(), allocator);

                    rapidjson::Value textColor;
                    SetVec4(textColor, ui.textColor);
                    uiObj.AddMember("text_color", textColor.Move(), allocator);

                    rapidjson::Value imageTint;
                    SetVec4(imageTint, ui.imageTint);
                    uiObj.AddMember("image_tint", imageTint.Move(), allocator);

                    rapidjson::Value anchorV(rapidjson::kArrayType);
                    anchorV.PushBack(SafeFloat(ui.anchor.x), allocator).PushBack(SafeFloat(ui.anchor.y), allocator);
                    uiObj.AddMember("anchor", anchorV.Move(), allocator);

                    rapidjson::Value pivotV(rapidjson::kArrayType);
                    pivotV.PushBack(SafeFloat(ui.pivot.x), allocator).PushBack(SafeFloat(ui.pivot.y), allocator);
                    uiObj.AddMember("pivot", pivotV.Move(), allocator);

                    uiObj.AddMember("font_scale", SafeFloat(ui.fontScale), allocator);
                    uiObj.AddMember("text_align_h", static_cast<int>(ui.textAlignH), allocator);
                    uiObj.AddMember("text_align_v", static_cast<int>(ui.textAlignV), allocator);

                    rapidjson::Value textOffsetV(rapidjson::kArrayType);
                    textOffsetV.PushBack(SafeFloat(ui.textOffset.x), allocator).PushBack(SafeFloat(ui.textOffset.y), allocator);
                    uiObj.AddMember("text_offset", textOffsetV.Move(), allocator);

                    uiObj.AddMember("visible", ui.visible, allocator);
                    uiObj.AddMember("draggable", ui.draggable, allocator);
                    uiObj.AddMember("no_input", ui.noInput, allocator);
                    uiObj.AddMember("bring_to_front", ui.bringToFront, allocator);

                    nodeObj.AddMember("runtime_ui", uiObj.Move(), allocator);
                }

                if ((flags & Component_Sprite) && cache.sprite)
                {
                    const NodeSpriteComponent &sprite = *cache.sprite;
                    rapidjson::Value spriteObj(rapidjson::kObjectType);
                    if (!sprite.imagePath.empty())
                        spriteObj.AddMember("image", MakeStringValue(SerializePath(sprite.imagePath, relativeToDir)), allocator);
                    if (!sprite.metadataPath.empty())
                        spriteObj.AddMember("metadata", MakeStringValue(SerializePath(sprite.metadataPath, relativeToDir)), allocator);
                    if (!sprite.frameName.empty())
                        spriteObj.AddMember("frame_name", MakeStringValue(sprite.frameName), allocator);
                    spriteObj.AddMember("frame_index", sprite.frameIndex, allocator);
                    spriteObj.AddMember("image_width", sprite.imageWidth, allocator);
                    spriteObj.AddMember("image_height", sprite.imageHeight, allocator);
                    spriteObj.AddMember("frame_width", sprite.frameWidth, allocator);
                    spriteObj.AddMember("frame_height", sprite.frameHeight, allocator);
                    spriteObj.AddMember("quad_width", SafeFloat(sprite.quadWidth), allocator);
                    spriteObj.AddMember("quad_height", SafeFloat(sprite.quadHeight), allocator);

                    rapidjson::Value uvRect;
                    SetVec4(uvRect, sprite.uvRect);
                    spriteObj.AddMember("uv_rect", uvRect.Move(), allocator);

                    rapidjson::Value tint;
                    SetVec4(tint, sprite.tint);
                    spriteObj.AddMember("tint", tint.Move(), allocator);

                    if (!sprite.activeClipName.empty())
                        spriteObj.AddMember("active_clip", MakeStringValue(sprite.activeClipName), allocator);
                    spriteObj.AddMember("active_clip_index", sprite.activeClipIndex, allocator);
                    spriteObj.AddMember("mesh_slot", sprite.meshSlot, allocator);
                    spriteObj.AddMember("playback_speed", SafeFloat(sprite.playbackSpeed), allocator);
                    spriteObj.AddMember("loop", sprite.loop, allocator);

                    nodeObj.AddMember("sprite", spriteObj.Move(), allocator);
                }

                if (scene.NodeUsesSkinnedStrip2D(node) && cache.skinnedStrip2D)
                {
                    const NodeSkinnedStrip2DComponent &strip = *cache.skinnedStrip2D;
                    rapidjson::Value stripObj(rapidjson::kObjectType);

                    rapidjson::Value target;
                    SetVec2(target, strip.ikTargetLocal);
                    stripObj.AddMember("ik_target", target.Move(), allocator);
                    stripObj.AddMember("ik_iterations", strip.ikIterations, allocator);
                    stripObj.AddMember("max_bend_degrees", SafeFloat(strip.maxBendDegrees), allocator);
                    stripObj.AddMember("bend_sign", SafeFloat(strip.bendSign), allocator);
                    stripObj.AddMember("stretch_scale", SafeFloat(strip.stretchScale), allocator);
                    stripObj.AddMember("max_stretch_scale", SafeFloat(strip.maxStretchScale), allocator);

                    rapidjson::Value rotations(rapidjson::kArrayType);
                    for (float rotation : strip.rotationsRadians)
                        rotations.PushBack(SafeFloat(rotation), allocator);
                    stripObj.AddMember("rotations", rotations.Move(), allocator);

                    rapidjson::Value influences(rapidjson::kArrayType);
                    for (float influence : strip.jointInfluences)
                        influences.PushBack(SafeFloat(std::clamp(influence, 0.0f, 2.0f)), allocator);
                    stripObj.AddMember("joint_influences", influences.Move(), allocator);

                    rapidjson::Value widthScales(rapidjson::kArrayType);
                    for (float widthScale : strip.widthScales)
                        widthScales.PushBack(SafeFloat(std::clamp(widthScale, 0.05f, 3.0f)), allocator);
                    stripObj.AddMember("width_scales", widthScales.Move(), allocator);

                    nodeObj.AddMember("skinned_strip_2d", stripObj.Move(), allocator);
                }

                nodesArr.PushBack(nodeObj.Move(), allocator);
            }

            document.AddMember("nodes", nodesArr.Move(), allocator);
        }

        void AddLegacyLights()
        {
            rapidjson::Value lights(rapidjson::kArrayType);

            for (const auto &light : scene.m_directionalLights)
            {
                rapidjson::Value lightObj(rapidjson::kObjectType);
                lightObj.AddMember("type", "directional", allocator);
                rapidjson::Value color;
                rapidjson::Value pos;
                rapidjson::Value rot;
                SetVec4(color, light.color);
                SetVec4(pos, light.position);
                SetVec4(rot, light.rotation);
                lightObj.AddMember("color", color.Move(), allocator);
                lightObj.AddMember("position", pos.Move(), allocator);
                lightObj.AddMember("rotation", rot.Move(), allocator);
                lightObj.AddMember("name", MakeStringValue(light.name), allocator);
                lights.PushBack(lightObj.Move(), allocator);
            }

            for (const auto &light : scene.m_pointLights)
            {
                rapidjson::Value lightObj(rapidjson::kObjectType);
                lightObj.AddMember("type", "point", allocator);
                rapidjson::Value color;
                rapidjson::Value pos;
                SetVec4(color, light.color);
                SetVec4(pos, light.position);
                lightObj.AddMember("color", color.Move(), allocator);
                lightObj.AddMember("position", pos.Move(), allocator);
                lightObj.AddMember("name", MakeStringValue(light.name), allocator);
                lights.PushBack(lightObj.Move(), allocator);
            }

            for (const auto &light : scene.m_spotLights)
            {
                rapidjson::Value lightObj(rapidjson::kObjectType);
                lightObj.AddMember("type", "spot", allocator);
                rapidjson::Value color;
                rapidjson::Value pos;
                rapidjson::Value rot;
                rapidjson::Value params;
                SetVec4(color, light.color);
                SetVec4(pos, light.position);
                SetVec4(rot, light.rotation);
                SetVec4(params, light.params);
                lightObj.AddMember("color", color.Move(), allocator);
                lightObj.AddMember("position", pos.Move(), allocator);
                lightObj.AddMember("rotation", rot.Move(), allocator);
                lightObj.AddMember("params", params.Move(), allocator);
                lightObj.AddMember("name", MakeStringValue(light.name), allocator);
                lights.PushBack(lightObj.Move(), allocator);
            }

            for (const auto &light : scene.m_areaLights)
            {
                rapidjson::Value lightObj(rapidjson::kObjectType);
                lightObj.AddMember("type", "area", allocator);
                rapidjson::Value color;
                rapidjson::Value pos;
                rapidjson::Value rot;
                rapidjson::Value size;
                SetVec4(color, light.color);
                SetVec4(pos, light.position);
                SetVec4(rot, light.rotation);
                SetVec4(size, light.size);
                lightObj.AddMember("color", color.Move(), allocator);
                lightObj.AddMember("position", pos.Move(), allocator);
                lightObj.AddMember("rotation", rot.Move(), allocator);
                lightObj.AddMember("size", size.Move(), allocator);
                lightObj.AddMember("name", MakeStringValue(light.name), allocator);
                lights.PushBack(lightObj.Move(), allocator);
            }

            document.AddMember("lights", lights.Move(), allocator);
        }

        void AddLegacyCameras(LegacyCameraMode mode)
        {
            rapidjson::Value cameras(rapidjson::kArrayType);
            Camera *activeCamera = (mode == LegacyCameraMode::Snapshot && !scene.m_cameras.empty()) ? scene.GetActiveCamera() : nullptr;

            for (auto *camera : scene.m_cameras)
            {
                if (!camera)
                    continue;

                rapidjson::Value camObj(rapidjson::kObjectType);
                camObj.AddMember("name", MakeStringValue(camera->GetName()), allocator);

                if (mode == LegacyCameraMode::SaveFile || camera != activeCamera)
                {
                    rapidjson::Value pos;
                    rapidjson::Value eul;
                    SetVec3(pos, camera->GetPosition());
                    SetVec3(eul, camera->GetEuler());
                    camObj.AddMember("position", pos.Move(), allocator);
                    camObj.AddMember("euler", eul.Move(), allocator);
                }

                camObj.AddMember("fovx", camera->Fovx(), allocator);
                camObj.AddMember("projection", MakeStringValue(camera->IsOrthographic() ? "orthographic" : "perspective"), allocator);
                camObj.AddMember("orthographic_size", camera->GetOrthographicSize(), allocator);
                camObj.AddMember("near_plane", camera->GetNearPlane(), allocator);
                camObj.AddMember("far_plane", camera->GetFarPlane(), allocator);
                camObj.AddMember("speed", camera->GetSpeed(), allocator);

                NodeId *camNode = camera->GetNodeId();
                if (camNode)
                    camObj.AddMember("node_index", static_cast<int>(camNode->index), allocator);

                cameras.PushBack(camObj.Move(), allocator);
            }

            document.AddMember("cameras", cameras.Move(), allocator);
        }

        void AddActiveCamera()
        {
            for (int i = 0; i < static_cast<int>(scene.m_cameras.size()); i++)
            {
                if (scene.m_cameras[i] == scene.GetActiveCamera())
                {
                    document.AddMember("active_camera", i, allocator);
                    break;
                }
            }
        }

        void AddEmitters()
        {
            if (!scene.m_particleManager)
                return;

            rapidjson::Value emitters(rapidjson::kArrayType);
            const auto &emitterList = scene.m_particleManager->GetEmitters();
            const auto &emitterNames = scene.m_particleManager->GetEmitterNames();
            for (size_t i = 0; i < emitterList.size(); i++)
            {
                const auto &emitter = emitterList[i];
                rapidjson::Value emitterObj(rapidjson::kObjectType);

                std::string name = (i < emitterNames.size()) ? emitterNames[i] : ("Emitter " + std::to_string(i));
                emitterObj.AddMember("name", MakeStringValue(name), allocator);

                rapidjson::Value pos;
                rapidjson::Value vel;
                rapidjson::Value colorStart;
                rapidjson::Value colorEnd;
                rapidjson::Value sizeLife;
                rapidjson::Value physics;
                rapidjson::Value gravity;
                rapidjson::Value animation;
                SetVec4(pos, emitter.position);
                SetVec4(vel, emitter.velocity);
                SetVec4(colorStart, emitter.colorStart);
                SetVec4(colorEnd, emitter.colorEnd);
                SetVec4(sizeLife, emitter.sizeLife);
                SetVec4(physics, emitter.physics);
                SetVec4(gravity, emitter.gravity);
                SetVec4(animation, emitter.animation);
                emitterObj.AddMember("position", pos.Move(), allocator);
                emitterObj.AddMember("velocity", vel.Move(), allocator);
                emitterObj.AddMember("color_start", colorStart.Move(), allocator);
                emitterObj.AddMember("color_end", colorEnd.Move(), allocator);
                emitterObj.AddMember("size_life", sizeLife.Move(), allocator);
                emitterObj.AddMember("physics", physics.Move(), allocator);
                emitterObj.AddMember("gravity", gravity.Move(), allocator);
                emitterObj.AddMember("animation", animation.Move(), allocator);
                emitterObj.AddMember("texture_index", emitter.textureIndex, allocator);
                emitterObj.AddMember("count", emitter.count, allocator);
                emitterObj.AddMember("orientation", emitter.orientation, allocator);
                emitters.PushBack(emitterObj.Move(), allocator);
            }

            document.AddMember("emitters", emitters.Move(), allocator);
        }
    };

    std::string Scene::GetSceneName() const
    {
        if (m_scenePath.empty())
            return "Untitled";
        return m_scenePath.stem().string();
    }

    void Scene::SaveScene(const std::filesystem::path &file)
    {
        rapidjson::Document d;
        d.SetObject();
        const std::filesystem::path sceneDir = file.parent_path();
        SceneSerializationHelper serializer(*this, d);
        serializer.AddSettings();
        serializer.AddSceneScripts();
        serializer.BuildLiveGeometryRemap();
        serializer.AddSources(&sceneDir);
        serializer.AddMeshes(&sceneDir);
        serializer.AddNodes(&sceneDir);
        serializer.AddLegacyLights();
        serializer.AddLegacyCameras(SceneSerializationHelper::LegacyCameraMode::SaveFile);
        serializer.AddActiveCamera();

        // Write to file
        std::ofstream ofs(file);
        if (!ofs.is_open())
        {
            Log::Error("Failed to open file for writing: " + file.string());
            return;
        }

        rapidjson::OStreamWrapper osw(ofs);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
        writer.SetMaxDecimalPlaces(6);

        if (d.Accept(writer))
        {
            if (ofs.bad())
            {
                Log::Error("File stream error while writing to: " + file.string());
            }
            else
            {
                m_scenePath = file;
                ClearDirty();
                Log::Info("Scene saved to: " + file.string());
            }
        }
        else
        {
            Log::Error("Failed to write JSON content (invalid data/encoding?) to: " + file.string());
        }
    }

    bool Scene::SavePrefab(NodeId *root, const std::filesystem::path &file)
    {
        if (!IsNodeAlive(root))
        {
            PE_WARN("[Prefab] Save skipped: invalid root node");
            return false;
        }

        std::filesystem::path prefabPath = file;
        if (prefabPath.extension() != ".peprefab")
            prefabPath += ".peprefab";

        std::error_code ec;
        if (!prefabPath.parent_path().empty())
            std::filesystem::create_directories(prefabPath.parent_path(), ec);
        if (ec)
        {
            PE_WARN("[Prefab] Failed to create prefab directory '%s': %s",
                    prefabPath.parent_path().string().c_str(), ec.message().c_str());
            return false;
        }

        rapidjson::Document d;
        d.SetObject();
        auto &allocator = d.GetAllocator();
        d.AddMember("asset_type", "prefab", allocator);
        d.AddMember("version", 1, allocator);
        d.AddMember("root", 0, allocator);

        const std::filesystem::path prefabDir = prefabPath.parent_path();
        SceneSerializationHelper serializer(*this, d);
        if (!serializer.BuildSubtreeGeometryRemap(root))
            return false;
        serializer.skipPrefabPathNode = root;

        serializer.AddSources(&prefabDir);
        serializer.AddMeshes(&prefabDir);
        serializer.AddNodes(&prefabDir);

        std::ofstream ofs(prefabPath);
        if (!ofs.is_open())
        {
            Log::Error("Failed to open prefab for writing: " + prefabPath.string());
            return false;
        }

        rapidjson::OStreamWrapper osw(ofs);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
        writer.SetMaxDecimalPlaces(6);
        if (!d.Accept(writer) || ofs.bad())
        {
            Log::Error("Failed to write prefab: " + prefabPath.string());
            return false;
        }

        SetNodePrefabPath(root, prefabPath.generic_string());
        Log::Info("Prefab saved to: " + prefabPath.string());
        return true;
    }

    void Scene::NewScene()
    {
        m_generation++;
        m_scenePath.clear();
        m_dirty = false;
        m_scriptManifest.Clear();

        if (IsScenePhysicsSimulating())
            StopScenePhysicsSimulation();
        ClearScenePhysicsBodies();
#ifdef PE_PHYSICS2D
        ClearScenePhysics2DBodies();
#endif

        ClearSceneSelection();
        ClearSceneAnimations();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();
        m_primitiveGeometryCache.clear();

        // Null out camera nodeIds before freeing node memory
        for (auto *cam : m_cameras)
            if (cam)
                cam->SetNodeId(nullptr);

        // Null out light nodeIds before freeing node memory
        for (auto &l : m_directionalLights)
            l.nodeId = nullptr;
        for (auto &l : m_pointLights)
            l.nodeId = nullptr;
        for (auto &l : m_spotLights)
            l.nodeId = nullptr;
        for (auto &l : m_areaLights)
            l.nodeId = nullptr;

        DestroyAllNodeEntities();

        RetireAllNodeIds();

        m_nodeRuntime.clear();
        m_nodesMoved.clear();
        m_nodesDirty = false;

        m_meshes.clear();
        m_meshRuntimes.clear();
        m_ownedMaterialInstances.clear();
        m_ownedMaterials.clear();
        m_vertexStore.clear();
        m_positionUvStore.clear();
        m_aabbVertexStore.clear();
        m_indexStore.clear();
        m_imageStore.clear();
        m_samplerStore.clear();

        ResetSkeletonCache();
        for (auto *model : m_models)
            delete model;
        m_models.clear();

        m_directionalLights.clear();
        m_pointLights.clear();
        m_spotLights.clear();
        m_areaLights.clear();

        while (m_cameras.size() > 1)
        {
            delete m_cameras.back();
            m_cameras.pop_back();
        }

        // Recreate scene node for the remaining camera
        if (!m_cameras.empty() && m_cameras[0])
        {
            NodeId *camNode = CreateNode(m_cameras[0]->GetName());
            AddComponentFlag(camNode, Component_Camera);
            m_nodeComponentCache[camNode->index].camera->camera = m_cameras[0];
            m_cameras[0]->SetNodeId(camNode);
        }
        EnsureSkyboxNodeFromSettings(false);
        // New (empty) scenes get a Scene Settings node by default so rendering is on out of the box.
        // Loaded scenes are NOT force-given one (opt-in: a deleted/absent node means post-process +
        // shadows off; culling/grid/debug toggles are unaffected).
        EnsureSceneSettingsNodeFromSettings(false);

        UpdateGeometryBuffers();

        Log::Info("New scene created.");
    }

    Scene::ScenePreload::~ScenePreload()
    {
        for (auto *m : models)
            delete m;
    }

    Scene::ScenePreload Scene::PreloadScene(const std::filesystem::path &file)
    {
        ScenePreload result;
        result.filePath = file;

        std::ifstream ifs(file);
        if (!ifs.is_open())
        {
            Log::Error("Failed to open scene file: " + file.string());
            return result;
        }
        result.jsonText = std::string(std::istreambuf_iterator<char>(ifs),
                                      std::istreambuf_iterator<char>());

        rapidjson::Document d;
        d.Parse(result.jsonText.c_str());
        if (d.HasParseError())
        {
            Log::Error("Failed to parse scene file: " + file.string());
            return result;
        }

        // Set progress for Loading widget
        auto &loading = Settings::Get<SceneSettings>().loading;
        uint32_t modelCount = 0;
        if (d.HasMember("sources"))
            modelCount = d["sources"].Size();
        else if (d.HasMember("models"))
            modelCount = d["models"].Size();
        loading.total.store(modelCount, std::memory_order_relaxed);
        loading.current.store(0, std::memory_order_relaxed);
        loading.SetName("Loading scene models");

        if (d.HasMember("sources"))
        {
            const auto &sourcesVal = d["sources"];
            result.models.resize(sourcesVal.Size(), nullptr);
            for (rapidjson::SizeType si = 0; si < sourcesVal.Size(); si++)
            {
                const auto &sv = sourcesVal[si];
                ModelAsset *model = nullptr;
                if (sv.HasMember("primitive_type"))
                {
                    uint32_t paramCount = 0;
                    vec4 params = ReadPrimitiveParams(sv, paramCount);
                    model = CreatePrimitiveModelFromSource(sv["primitive_type"].GetString(), params, paramCount);
                }
                else if (sv.HasMember("path"))
                {
                    std::filesystem::path modelPath = sv["path"].GetString();
                    if (modelPath.is_relative())
                        modelPath = file.parent_path() / modelPath;
                    modelPath = modelPath.lexically_normal();
                    if (!std::filesystem::is_directory(modelPath))
                        model = ModelAsset::Load(modelPath);
                }
                result.models[si] = model;
                loading.current.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else if (d.HasMember("models"))
        {
            const auto &modelsVal = d["models"];
            result.models.resize(modelsVal.Size(), nullptr);
            for (rapidjson::SizeType i = 0; i < modelsVal.Size(); i++)
            {
                const auto &modelVal = modelsVal[i];
                ModelAsset *model = nullptr;
                if (modelVal.HasMember("primitive_type"))
                {
                    uint32_t paramCount = 0;
                    vec4 params = ReadPrimitiveParams(modelVal, paramCount);
                    model = CreatePrimitiveModelFromSource(modelVal["primitive_type"].GetString(), params, paramCount);
                }
                else if (modelVal.HasMember("path"))
                {
                    std::filesystem::path modelPath = modelVal["path"].GetString();
                    if (modelPath.is_relative())
                        modelPath = file.parent_path() / modelPath;
                    modelPath = modelPath.lexically_normal();
                    if (std::filesystem::is_directory(modelPath))
                        continue;
                    model = ModelAsset::Load(modelPath);
                }
                result.models[i] = model;
                loading.current.fetch_add(1, std::memory_order_relaxed);
            }
        }

        result.valid = true;
        return result;
    }

    void Scene::LoadSceneApply(ScenePreload preload)
    {
        if (!preload.valid)
            return;

        m_generation++;
        m_scenePath = preload.filePath;
        m_dirty = false;

        m_autoplayAnimations = false;

        rapidjson::Document d;
        d.Parse(preload.jsonText.c_str());
        if (d.HasParseError())
        {
            m_autoplayAnimations = true;
            return;
        }
        const std::filesystem::path sceneDir = preload.filePath.parent_path();

        // Clear all physics bodies before freeing node memory
        if (IsScenePhysicsSimulating())
            StopScenePhysicsSimulation();
        ClearScenePhysicsBodies();
#ifdef PE_PHYSICS2D
        ClearScenePhysics2DBodies();
#endif
        ClearSceneAudioSources();
        ClearSceneAnimations();

        // Clear existing scene: free SoA data, delete models, clear geometry stores
        ClearSceneSelection();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();
        m_primitiveGeometryCache.clear();

        // Null out camera nodeIds before freeing node memory
        for (auto *cam : m_cameras)
            if (cam)
                cam->SetNodeId(nullptr);

        // Null out light nodeIds before freeing node memory
        for (auto &l : m_directionalLights)
            l.nodeId = nullptr;
        for (auto &l : m_pointLights)
            l.nodeId = nullptr;
        for (auto &l : m_spotLights)
            l.nodeId = nullptr;
        for (auto &l : m_areaLights)
            l.nodeId = nullptr;

        DestroyAllNodeEntities();

        RetireAllNodeIds();

        m_nodeRuntime.clear();
        m_nodesMoved.clear();
        m_nodesDirty = false;

        m_meshes.clear();
        m_meshRuntimes.clear();
        m_ownedMaterialInstances.clear();
        m_ownedMaterials.clear();
        m_vertexStore.clear();
        m_positionUvStore.clear();
        m_aabbVertexStore.clear();
        m_indexStore.clear();
        m_imageStore.clear();
        m_samplerStore.clear();

        ResetSkeletonCache();
        for (auto *model : m_models)
            delete model;
        m_models.clear();

        m_directionalLights.clear();
        m_pointLights.clear();
        m_spotLights.clear();
        m_areaLights.clear();

        // Reuse first camera, remove others
        while (m_cameras.size() > 1)
        {
            delete m_cameras.back();
            m_cameras.pop_back();
        }

        if (d.HasMember("settings"))
        {
            const auto &settings = d["settings"];
            ApplySceneSettingsMembers(settings);
            MarkUniformsDirty();
        }

        ParseSceneScriptManifest(d, m_scriptManifest);

        auto ReadVec3 = [](const rapidjson::Value &arr)
        {
            return vec3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat());
        };
        auto ReadVec4 = [](const rapidjson::Value &arr)
        {
            return vec4(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat());
        };
        auto ReadMat4 = [](const rapidjson::Value &arr)
        {
            mat4 m;
            float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                p[i] = arr[i].GetFloat();
            return m;
        };

        bool hadEmbeddedCameras = false;
        bool hadEmbeddedLights = false;

        if (d.HasMember("sources"))
        {
            // --- New flat format ---
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            // 1. Load sources and copy geometry
            const auto &sourcesVal = d["sources"];
            std::vector<ModelAsset *> loadedModels(sourcesVal.Size(), nullptr);
            std::vector<std::vector<int>> sourceMeshMaps(sourcesVal.Size());

            for (rapidjson::SizeType si = 0; si < sourcesVal.Size(); si++)
            {
                ModelAsset *model = si < preload.models.size() ? preload.models[si] : nullptr;
                if (si < preload.models.size())
                    preload.models[si] = nullptr; // take ownership

                if (model)
                {
                    int sourceIndex = static_cast<int>(m_sources.size());
                    SceneSource source;
                    source.filePath = model->GetFilePath();
                    source.primitiveType = model->GetPrimitiveType();
                    source.primitiveParams = model->GetPrimitiveParams();
                    source.primitiveParamCount = model->GetPrimitiveParamCount();
                    source.modelId = model->GetId();
                    m_sources.push_back(std::move(source));

                    sourceMeshMaps[si] = AddModelGeometry(model, sourceIndex);
                    m_models.insert(model->GetId(), model);
                    loadedModels[si] = model;
                }
            }

            // 2. Apply mesh overrides from JSON
            std::vector<int> savedMeshToSceneMesh;
            if (d.HasMember("meshes"))
            {
                const auto &meshesVal = d["meshes"];
                savedMeshToSceneMesh.assign(meshesVal.Size(), -1);
                std::unordered_set<Material *> loadedSharedMaterials;

                for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
                {
                    const auto &mVal = meshesVal[mi];

                    // Resolve scene mesh index via source reference
                    int sceneMeshIdx = -1;
                    if (mVal.HasMember("source") && mVal.HasMember("source_mesh"))
                    {
                        int srcIdx = mVal["source"].GetInt();
                        int srcMesh = mVal["source_mesh"].GetInt();
                        if (srcIdx >= 0 && srcIdx < static_cast<int>(sourceMeshMaps.size()) &&
                            srcMesh >= 0 && srcMesh < static_cast<int>(sourceMeshMaps[srcIdx].size()))
                        {
                            sceneMeshIdx = sourceMeshMaps[srcIdx][srcMesh];
                        }
                    }
                    if (sceneMeshIdx < 0 || sceneMeshIdx >= static_cast<int>(m_meshes.size()))
                        continue;

                    savedMeshToSceneMesh[mi] = sceneMeshIdx;

                    Mesh &mesh = m_meshes[sceneMeshIdx];
                    if (mVal.HasMember("render_type"))
                        mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                    if (mVal.HasMember("lod_shift"))
                        mesh.lodShift = mVal["lod_shift"].GetUint();
                    if (mVal.HasMember("lod_enabled"))
                        mesh.lodEnabled = mVal["lod_enabled"].GetBool();
                    if (mVal.HasMember("lod_bias"))
                        mesh.lodBias = mVal["lod_bias"].GetFloat();

                    bool isInstanced = mVal.HasMember("instanced") && mVal["instanced"].GetBool();

                    // For instanced meshes: load into a temp Material, then create a MaterialInstance with diffs.
                    // For shared meshes: only load into the Material on the first occurrence.
                    Material *target = mesh.material;
                    Material tempMat;
                    if (isInstanced && target)
                        tempMat = *target; // start from parent as base

                    Material *loadTarget = isInstanced ? &tempMat : target;
                    bool skipShared = !isInstanced && target && loadedSharedMaterials.count(target) > 0;

                    if (loadTarget && !skipShared)
                    {
                        loadTarget->renderType = mesh.renderType;
                        if (mVal.HasMember("double_sided"))
                            loadTarget->doubleSided = mVal["double_sided"].GetBool();
                        if (mVal.HasMember("texture_mask"))
                        {
                            uint32_t savedMask = mVal["texture_mask"].GetUint();
                            if (savedMask != 0 || loadTarget->textureMask == 0)
                                loadTarget->textureMask = savedMask;
                        }
                        if (mVal.HasMember("material_factors"))
                        {
                            mat4 factors[2];
                            factors[0] = ReadMat4(mVal["material_factors"][0]);
                            factors[1] = ReadMat4(mVal["material_factors"][1]);
                            DecodeMaterialFactorsFromPersistence(mesh.renderType, factors[0], factors[1]);
                            loadTarget->UnpackLegacyFactors(factors, loadTarget->textureMask);
                        }

                        if (mVal.HasMember("textures"))
                        {
                            const auto &texVal = mVal["textures"];
                            const char *texSlotNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                            for (int k = 0; k < 5; k++)
                            {
                                if (texVal.HasMember(texSlotNames[k]) && texVal[texSlotNames[k]].GetStringLength() > 0)
                                {
                                    std::filesystem::path texPath = texVal[texSlotNames[k]].GetString();
                                    if (texPath.is_relative())
                                        texPath = (preload.filePath.parent_path() / texPath).lexically_normal();
                                    if (std::filesystem::exists(texPath))
                                    {
                                        int srcIdx = m_meshSourceInfos[sceneMeshIdx].sourceIndex;
                                        ModelAsset *srcModel = (srcIdx >= 0 && srcIdx < static_cast<int>(loadedModels.size()))
                                                                   ? loadedModels[srcIdx]
                                                                   : nullptr;
                                        if (srcModel)
                                        {
                                            ResourceHandle<Image> img = srcModel->LoadTexture(cmd, texPath);
                                            if (img)
                                                loadTarget->textures[k] = img;
                                        }
                                    }
                                }
                            }
                        }

                        if (!isInstanced && target)
                            loadedSharedMaterials.insert(target);
                    }

                    // Create MaterialInstance if this mesh was saved as instanced
                    if (isInstanced && target)
                    {
                        MaterialInstance *inst = CreateMaterialInstance(mesh);
                        if (inst)
                            inst->ApplyDiffFromResolved(tempMat);
                    }
                }
            }

            // 3. Create flat node hierarchy from JSON (two-pass: create, then reparent)
            if (d.HasMember("nodes"))
            {
                const auto &nodesVal = d["nodes"];
                std::vector<NodeId *> nodeMap(nodesVal.Size(), nullptr);

                // Pass 1: create all nodes as roots
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    std::string name = nv.HasMember("name") ? nv["name"].GetString() : "";
                    NodeId *node = CreateNode(name);
                    if (nv.HasMember("enabled") && nv["enabled"].IsBool())
                        SetNodeEnabled(node, nv["enabled"].GetBool());

                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]), false);
                    if (nv.HasMember("mesh_refs"))
                    {
                        const auto &arr = nv["mesh_refs"];
                        for (rapidjson::SizeType mi = 0; mi < arr.Size(); mi++)
                        {
                            int savedIdx = arr[mi].GetInt();
                            int sceneIdx = (savedIdx >= 0 && savedIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                               ? savedMeshToSceneMesh[savedIdx]
                                               : -1;
                            if (sceneIdx >= 0 && sceneIdx < static_cast<int>(m_meshes.size()))
                                AddMeshRef(node, sceneIdx);
                        }
                    }
                    else if (nv.HasMember("mesh"))
                    {
                        int savedMeshIdx = nv["mesh"].GetInt();
                        int sceneMeshIdx = (savedMeshIdx >= 0 && savedMeshIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                               ? savedMeshToSceneMesh[savedMeshIdx]
                                               : -1;
                        if (sceneMeshIdx >= 0 && sceneMeshIdx < static_cast<int>(m_meshes.size()))
                            SetMeshRef(node, sceneMeshIdx);
                    }
                    if (nv.HasMember("script"))
                    {
                        SetNodeScript(node, nv["script"].GetString());
                        if (nv.HasMember("scriptRunMode") && nv["scriptRunMode"].IsInt())
                            SetNodeScriptRunMode(node, static_cast<ScriptRunMode>(nv["scriptRunMode"].GetInt()));
                    }

                    nodeMap[ni] = node;
                }

                // Pass 2: set parents
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodeMap.size()) && nodeMap[parentIdx])
                            ReparentNode(nodeMap[ni], nodeMap[parentIdx]);
                    }
                }

                // Pass 3: set component flags and wire up cameras/lights from embedded node data
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node)
                        continue;

                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;
                    uint32_t restoreFlags = flags & ~(Component_Mesh | Component_Script | Component_GpuPending);
                    if (restoreFlags)
                        AddComponentFlag(node, restoreFlags);
                    RestorePrefabNode(*this, node, nv, &sceneDir);

                    if ((flags & Component_Camera) && nv.HasMember("camera"))
                    {
                        const auto &cv = nv["camera"];
                        Camera *cam = nullptr;
                        if (!m_cameras.empty() && !m_cameras[0]->GetNodeId())
                        {
                            cam = m_cameras[0];
                        }
                        else
                        {
                            cam = new Camera();
                            cam->SetName(nv.HasMember("name") ? nv["name"].GetString() : ("Camera_" + std::to_string(ID::NextID())));
                            m_cameras.push_back(cam);
                        }
                        if (cv.HasMember("projection") && cv["projection"].IsString())
                        {
                            std::string mode = cv["projection"].GetString();
                            cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                        }
                        else if (cv.HasMember("orthographic") && cv["orthographic"].IsBool())
                        {
                            cam->SetOrthographic(cv["orthographic"].GetBool());
                        }
                        if (cv.HasMember("fovx"))
                            cam->SetFovx(cv["fovx"].GetFloat());
                        if (cv.HasMember("orthographic_size"))
                            cam->SetOrthographicSize(cv["orthographic_size"].GetFloat());
                        if (cv.HasMember("near_plane"))
                            cam->SetNearPlane(cv["near_plane"].GetFloat());
                        if (cv.HasMember("far_plane"))
                            cam->SetFarPlane(cv["far_plane"].GetFloat());
                        if (cv.HasMember("speed"))
                            cam->SetSpeed(cv["speed"].GetFloat());
                        cam->SetNodeId(node);
                        if (m_nodeComponentCache[node->index].camera)
                            m_nodeComponentCache[node->index].camera->camera = cam;
                        const mat4 &lm = GetLocalMatrix(node);
                        cam->SetPosition(vec3(lm[3]));
                        if (cv.HasMember("euler"))
                            cam->SetEuler(ReadVec3(cv["euler"]));
                        else
                            cam->SetEuler(glm::eulerAngles(quat_cast(mat3(lm))));
                        hadEmbeddedCameras = true;
                    }

                    if ((flags & Component_Light) && nv.HasMember("light"))
                    {
                        const auto &lv = nv["light"];
                        std::string ltype = lv.HasMember("type") ? lv["type"].GetString() : "";
                        std::string lname = nv.HasMember("name") ? nv["name"].GetString() : "";
                        if (ltype == "directional")
                        {
                            SceneDirectionalLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.name = lname.empty() ? ("Directional Light " + std::to_string(ID::NextID())) : lname;
                            l.nodeId = node;
                            m_directionalLights.push_back(l);
                        }
                        else if (ltype == "point")
                        {
                            ScenePointLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("radius") ? lv["radius"].GetFloat() : 10.f;
                            l.name = lname.empty() ? ("Point Light " + std::to_string(ID::NextID())) : lname;
                            l.nodeId = node;
                            m_pointLights.push_back(l);
                        }
                        else if (ltype == "spot")
                        {
                            SceneSpotLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.params = lv.HasMember("params") ? ReadVec4(lv["params"]) : vec4(15.f, 5.f, 0.f, 0.f);
                            l.name = lname.empty() ? ("Spot Light " + std::to_string(ID::NextID())) : lname;
                            l.nodeId = node;
                            m_spotLights.push_back(l);
                        }
                        else if (ltype == "area")
                        {
                            SceneAreaLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.size = lv.HasMember("size") ? ReadVec4(lv["size"]) : vec4(2.f, 2.f, 0.f, 0.f);
                            l.name = lname.empty() ? ("Area Light " + std::to_string(ID::NextID())) : lname;
                            l.nodeId = node;
                            m_areaLights.push_back(l);
                        }
                        hadEmbeddedLights = true;
                    }
                }

                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestoreSkyboxNode(*this, nodeMap[ni], nodesVal[ni]);
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    RestoreTriggerZoneNode(*this, nodeMap[ni], nodesVal[ni]);
                }
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestoreVoxelWorldNode(*this, nodeMap[ni], nodesVal[ni]);
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestoreRuntimeUiNode(*this, nodeMap[ni], nodesVal[ni], &sceneDir);
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestoreSpriteNode(*this, nodeMap[ni], nodesVal[ni], &sceneDir);
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestoreSkinnedStrip2DNode(*this, nodeMap[ni], nodesVal[ni]);
                EnsureSkyboxNodeFromSettings(false);

                // Restore physics bodies from embedded node data
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node || !nv.HasMember("physics"))
                        continue;
                    const auto &pv = nv["physics"];
                    PhysicsBodyDesc desc;
                    if (pv.HasMember("body_type"))
                        desc.bodyType = static_cast<PhysicsBodyType>(pv["body_type"].GetInt());
                    if (pv.HasMember("shape_type"))
                        desc.shapeType = static_cast<PhysicsShapeType>(pv["shape_type"].GetInt());
                    if (pv.HasMember("mass"))
                        desc.mass = pv["mass"].GetFloat();
                    if (pv.HasMember("friction"))
                        desc.friction = pv["friction"].GetFloat();
                    if (pv.HasMember("restitution"))
                        desc.restitution = pv["restitution"].GetFloat();
                    if (pv.HasMember("auto_fit"))
                        desc.autoFitShape = pv["auto_fit"].GetBool();
                    if (pv.HasMember("is_trigger"))
                        desc.isTrigger = pv["is_trigger"].GetBool();
                    if (pv.HasMember("box_half_extents"))
                        desc.boxHalfExtents = ReadVec3(pv["box_half_extents"]);
                    if (pv.HasMember("sphere_radius"))
                        desc.sphereRadius = pv["sphere_radius"].GetFloat();
                    if (pv.HasMember("capsule_half_height"))
                        desc.capsuleHalfHeight = pv["capsule_half_height"].GetFloat();
                    if (pv.HasMember("capsule_radius"))
                        desc.capsuleRadius = pv["capsule_radius"].GetFloat();
                    AddScenePhysicsBody(*this, node, desc);
                }

#ifdef PE_PHYSICS2D
                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                    RestorePhysics2DNode(*this, nodeMap[ni], nodesVal[ni]);
#endif

                for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
                {
                    const auto &nv = nodesVal[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node || !nv.HasMember("audio"))
                        continue;
                    const auto &av = nv["audio"];
                    AudioSourceDesc desc;
                    if (av.HasMember("file"))
                        desc.filePath = av["file"].GetString();
                    if (av.HasMember("volume"))
                        desc.volume = av["volume"].GetFloat();
                    if (av.HasMember("pitch"))
                        desc.pitch = av["pitch"].GetFloat();
                    if (av.HasMember("min_distance"))
                        desc.minDistance = av["min_distance"].GetFloat();
                    if (av.HasMember("max_distance"))
                        desc.maxDistance = av["max_distance"].GetFloat();
                    if (av.HasMember("loop"))
                        desc.loop = av["loop"].GetBool();
                    if (av.HasMember("spatial"))
                        desc.spatial = av["spatial"].GetBool();
                    if (av.HasMember("autoplay"))
                        desc.autoplay = av["autoplay"].GetBool();
                    AddSceneAudioSource(*this, node, desc);
                }

                // Mark all nodes dirty
                for (NodeId *node : nodeMap)
                {
                    if (node)
                        MarkNodeDirty(node);
                }
                UpdateNodeMatrices();
            }

            UploadBuffers(cmd);

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }
        else if (d.HasMember("models"))
        {
            // --- Legacy per-model format ---
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            const auto &models = d["models"];
            for (rapidjson::SizeType i = 0; i < models.Size(); i++)
            {
                const auto &modelVal = models[i];
                ModelAsset *model = i < preload.models.size() ? preload.models[i] : nullptr;
                if (i < preload.models.size())
                    preload.models[i] = nullptr; // take ownership

                if (model)
                {
                    if (modelVal.HasMember("name"))
                        model->SetLabel(modelVal["name"].GetString());
                    if (modelVal.HasMember("matrix"))
                        model->SetMatrix(ReadMat4(modelVal["matrix"]));

                    if (modelVal.HasMember("nodes"))
                    {
                        const auto &nodesVal = modelVal["nodes"];
                        for (rapidjson::SizeType i = 0; i < nodesVal.Size() && i < static_cast<rapidjson::SizeType>(model->GetNodeCount()); i++)
                        {
                            const auto &nVal = nodesVal[i];
                            if (nVal.HasMember("name"))
                                model->SetNodeName(static_cast<int>(i), nVal["name"].GetString());
                            if (nVal.HasMember("parent"))
                                model->SetNodeParentIndex(static_cast<int>(i), nVal["parent"].GetInt());
                            if (nVal.HasMember("local_matrix"))
                                model->SetNodeLocalMatrix(static_cast<int>(i), ReadMat4(nVal["local_matrix"]));
                        }

                        model->RebuildNodeChildrenFromParents();
                    }

                    if (modelVal.HasMember("meshes"))
                    {
                        const auto &meshesVal = modelVal["meshes"];
                        std::unordered_set<Material *> loadedModelMaterials;

                        for (rapidjson::SizeType i = 0; i < meshesVal.Size(); i++)
                        {
                            MeshInfo *mi = model->GetMeshInfo(static_cast<int>(i));
                            if (!mi)
                                break;

                            const auto &mVal = meshesVal[i];
                            if (mVal.HasMember("render_type"))
                                mi->renderType = static_cast<RenderType>(mVal["render_type"].GetInt());

                            // Skip material data for shared materials already loaded
                            if (mi->material && loadedModelMaterials.count(mi->material) > 0)
                                continue;

                            if (mVal.HasMember("material_factors") && mi->material)
                            {
                                mat4 factors[2];
                                factors[0] = ReadMat4(mVal["material_factors"][0]);
                                factors[1] = ReadMat4(mVal["material_factors"][1]);
                                DecodeMaterialFactorsFromPersistence(mi->renderType, factors[0], factors[1]);
                                mi->material->UnpackLegacyFactors(factors, mi->material->textureMask);
                            }

                            bool hasTextureMask = mVal.HasMember("texture_mask");
                            if (hasTextureMask && mi->material)
                                mi->material->textureMask = mVal["texture_mask"].GetUint();

                            if (mVal.HasMember("textures"))
                            {
                                const auto &texVal = mVal["textures"];
                                const char *methodNames[] = {"base_color", "metallic_roughness", "normal", "occlusion", "emissive"};
                                for (int k = 0; k < 5; k++)
                                {
                                    if (texVal.HasMember(methodNames[k]) && texVal[methodNames[k]].GetStringLength() > 0)
                                    {
                                        std::filesystem::path texPath = texVal[methodNames[k]].GetString();
                                        if (texPath.is_relative())
                                            texPath = (preload.filePath.parent_path() / texPath).lexically_normal();
                                        if (std::filesystem::exists(texPath))
                                        {
                                            ResourceHandle<Image> img = model->LoadTexture(cmd, texPath);
                                            if (img && mi->material)
                                            {
                                                mi->material->textures[k] = img;
                                                if (!hasTextureMask)
                                                    mi->material->textureMask |= (1 << k);
                                            }
                                        }
                                    }
                                }
                            }

                            if (mi->material)
                                loadedModelMaterials.insert(mi->material);
                        }
                    }

                    EventSystem::PushEvent(EventType::ModelLoaded, model);
                }
            }

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }

        if (!hadEmbeddedLights && d.HasMember("lights"))
        {
            const auto &lights = d["lights"];
            for (const auto &lVal : lights.GetArray())
            {
                std::string type = lVal["type"].GetString();
                if (type == "directional")
                {
                    SceneDirectionalLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    if (lVal.HasMember("rotation"))
                        l.rotation = ReadVec4(lVal["rotation"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Directional Light " + std::to_string(ID::NextID());
                    m_directionalLights.push_back(l);
                }
                else if (type == "point")
                {
                    ScenePointLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Point Light " + std::to_string(ID::NextID());
                    m_pointLights.push_back(l);
                }
                else if (type == "spot")
                {
                    SceneSpotLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.params = ReadVec4(lVal["params"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Spot Light " + std::to_string(ID::NextID());
                    m_spotLights.push_back(l);
                }
                else if (type == "area")
                {
                    SceneAreaLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.size = ReadVec4(lVal["size"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Area Light " + std::to_string(ID::NextID());
                    m_areaLights.push_back(l);
                }
            }
        }

        if (!hadEmbeddedCameras && d.HasMember("cameras"))
        {
            const auto &cams = d["cameras"];
            for (rapidjson::SizeType i = 0; i < cams.Size(); ++i)
            {
                Camera *cam = nullptr;
                if (i < m_cameras.size())
                    cam = m_cameras[i];
                else
                {
                    cam = AddCamera();
                }

                const auto &cVal = cams[i];
                if (cVal.HasMember("name"))
                    cam->SetName(cVal["name"].GetString());
                if (cVal.HasMember("position"))
                    cam->SetPosition(ReadVec3(cVal["position"]));
                if (cVal.HasMember("euler"))
                    cam->SetEuler(ReadVec3(cVal["euler"]));
                if (cVal.HasMember("projection") && cVal["projection"].IsString())
                {
                    std::string mode = cVal["projection"].GetString();
                    cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                }
                else if (cVal.HasMember("orthographic") && cVal["orthographic"].IsBool())
                {
                    cam->SetOrthographic(cVal["orthographic"].GetBool());
                }
                if (cVal.HasMember("fovx"))
                    cam->SetFovx(cVal["fovx"].GetFloat());
                if (cVal.HasMember("orthographic_size"))
                    cam->SetOrthographicSize(cVal["orthographic_size"].GetFloat());
                if (cVal.HasMember("near_plane"))
                    cam->SetNearPlane(cVal["near_plane"].GetFloat());
                if (cVal.HasMember("far_plane"))
                    cam->SetFarPlane(cVal["far_plane"].GetFloat());
                if (cVal.HasMember("speed"))
                    cam->SetSpeed(cVal["speed"].GetFloat());
            }
        }

        EnsureSkyboxNodeFromSettings(false);

        if (d.HasMember("active_camera"))
        {
            uint32_t activeIndex = d["active_camera"].GetUint();
            if (activeIndex < m_cameras.size())
                SetActiveCamera(m_cameras[activeIndex]);
        }

        m_autoplayAnimations = true;

        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            NodeId *node = m_nodeIds[ni];
            if (NodeHasSkinnedMesh(node) && !GetAnimationClipsForNode(node).empty() && !GetSkinnedStrip2DState(node))
                PlaySceneAnimation(*this, node, 0, true);
        }

        UpdateCameras(false);
        UpdateNodeMatrices();
        UpdateLights();
        ClearDirty();

        Log::Info("Scene loaded from: " + preload.filePath.string());
    }

    void Scene::LoadScene(const std::filesystem::path &file)
    {
        LoadSceneApply(PreloadScene(file));
    }

    SceneNodeHandle Scene::InstantiatePrefab(const std::filesystem::path &file, NodeId *parent)
    {
        if (parent && !IsNodeAlive(parent))
            parent = nullptr;

        std::ifstream ifs(file);
        if (!ifs.is_open())
        {
            PE_WARN("[Prefab] Failed to open prefab: %s", file.string().c_str());
            return {};
        }

        std::string jsonText{std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
        rapidjson::Document d;
        d.Parse(jsonText.c_str(), jsonText.size());
        if (d.HasParseError() || !d.IsObject() || !d.HasMember("nodes") || !d["nodes"].IsArray())
        {
            PE_WARN("[Prefab] Invalid prefab file: %s", file.string().c_str());
            return {};
        }

        const std::filesystem::path prefabDir = file.parent_path();

        auto ReadVec3 = [](const rapidjson::Value &arr, vec3 fallback = vec3(0.0f))
        {
            vec3 value = fallback;
            if (!arr.IsArray())
                return value;
            for (rapidjson::SizeType i = 0; i < arr.Size() && i < 3; ++i)
                if (arr[i].IsNumber())
                    value[static_cast<int>(i)] = arr[i].GetFloat();
            return value;
        };
        auto ReadVec4 = [](const rapidjson::Value &arr, vec4 fallback = vec4(0.0f))
        {
            vec4 value = fallback;
            if (!arr.IsArray())
                return value;
            for (rapidjson::SizeType i = 0; i < arr.Size() && i < 4; ++i)
                if (arr[i].IsNumber())
                    value[static_cast<int>(i)] = arr[i].GetFloat();
            return value;
        };
        auto ReadMat4 = [](const rapidjson::Value &arr, mat4 fallback = mat4(1.0f))
        {
            mat4 m = fallback;
            if (!arr.IsArray())
                return m;
            float *p = value_ptr(m);
            for (rapidjson::SizeType i = 0; i < arr.Size() && i < 16; ++i)
                if (arr[i].IsNumber())
                    p[i] = arr[i].GetFloat();
            return m;
        };

        bool needsTextureUpload = false;
        if (d.HasMember("meshes") && d["meshes"].IsArray())
        {
            const auto &meshesVal = d["meshes"];
            for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
            {
                const auto &mVal = meshesVal[mi];
                if (mVal.HasMember("textures") && mVal["textures"].IsObject() && mVal["textures"].MemberCount() > 0)
                {
                    needsTextureUpload = true;
                    break;
                }
            }
        }

        Queue *queue = needsTextureUpload ? RHII.GetMainQueue() : nullptr;
        CommandBuffer *cmd = queue ? queue->AcquireCommandBuffer() : nullptr;
        if (cmd)
            cmd->Begin();

        std::vector<std::vector<int>> sourceMeshMaps;
        if (d.HasMember("sources") && d["sources"].IsArray())
        {
            const auto &sourcesVal = d["sources"];
            sourceMeshMaps.resize(sourcesVal.Size());

            for (rapidjson::SizeType si = 0; si < sourcesVal.Size(); si++)
            {
                const auto &sv = sourcesVal[si];
                ModelAsset *model = nullptr;
                if (sv.HasMember("primitive_type") && sv["primitive_type"].IsString())
                {
                    uint32_t paramCount = 0;
                    vec4 params = ReadPrimitiveParams(sv, paramCount);
                    model = CreatePrimitiveModelFromSource(sv["primitive_type"].GetString(), params, paramCount);
                }
                else if (sv.HasMember("path"))
                {
                    std::filesystem::path modelPath = ResolveSerializedFilePath(sv["path"], &prefabDir);
                    if (!modelPath.empty() && !std::filesystem::is_directory(modelPath))
                        model = ModelAsset::Load(modelPath);
                }

                if (!model)
                    continue;

                int sourceIndex = static_cast<int>(m_sources.size());
                SceneSource source;
                source.filePath = model->GetFilePath();
                source.primitiveType = model->GetPrimitiveType();
                source.primitiveParams = model->GetPrimitiveParams();
                source.primitiveParamCount = model->GetPrimitiveParamCount();
                source.modelId = model->GetId();
                m_sources.push_back(std::move(source));

                sourceMeshMaps[si] = AddModelGeometry(model, sourceIndex);
                m_models.insert(model->GetId(), model);
            }
        }

        std::vector<int> savedMeshToSceneMesh;
        if (d.HasMember("meshes") && d["meshes"].IsArray())
        {
            const auto &meshesVal = d["meshes"];
            savedMeshToSceneMesh.assign(meshesVal.Size(), -1);
            std::unordered_set<Material *> loadedSharedMaterials;

            for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
            {
                const auto &mVal = meshesVal[mi];
                int sceneMeshIdx = -1;
                if (mVal.HasMember("source") && mVal.HasMember("source_mesh"))
                {
                    int srcIdx = mVal["source"].GetInt();
                    int srcMesh = mVal["source_mesh"].GetInt();
                    if (srcIdx >= 0 && srcIdx < static_cast<int>(sourceMeshMaps.size()) &&
                        srcMesh >= 0 && srcMesh < static_cast<int>(sourceMeshMaps[srcIdx].size()))
                    {
                        sceneMeshIdx = sourceMeshMaps[srcIdx][srcMesh];
                    }
                }
                if (sceneMeshIdx < 0 || sceneMeshIdx >= static_cast<int>(m_meshes.size()))
                    continue;

                savedMeshToSceneMesh[mi] = sceneMeshIdx;
                Mesh &mesh = m_meshes[sceneMeshIdx];
                if (mVal.HasMember("render_type"))
                    mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                if (mVal.HasMember("lod_shift"))
                    mesh.lodShift = mVal["lod_shift"].GetUint();
                if (mVal.HasMember("lod_enabled"))
                    mesh.lodEnabled = mVal["lod_enabled"].GetBool();
                if (mVal.HasMember("lod_bias"))
                    mesh.lodBias = mVal["lod_bias"].GetFloat();

                const bool isInstanced = mVal.HasMember("instanced") && mVal["instanced"].GetBool();
                Material *target = mesh.material;
                Material tempMat;
                if (isInstanced && target)
                    tempMat = *target;

                Material *loadTarget = isInstanced ? &tempMat : target;
                const bool skipShared = !isInstanced && target && loadedSharedMaterials.count(target) > 0;

                if (loadTarget && !skipShared)
                {
                    loadTarget->renderType = mesh.renderType;
                    if (mVal.HasMember("double_sided"))
                        loadTarget->doubleSided = mVal["double_sided"].GetBool();
                    if (mVal.HasMember("texture_mask"))
                        loadTarget->textureMask = mVal["texture_mask"].GetUint();
                    if (mVal.HasMember("material_factors") && mVal["material_factors"].IsArray() &&
                        mVal["material_factors"].Size() >= 2)
                    {
                        mat4 factors[2];
                        factors[0] = ReadMat4(mVal["material_factors"][0]);
                        factors[1] = ReadMat4(mVal["material_factors"][1]);
                        DecodeMaterialFactorsFromPersistence(mesh.renderType, factors[0], factors[1]);
                        loadTarget->UnpackLegacyFactors(factors, loadTarget->textureMask);
                    }
                    RestoreMaterialTexturesFromJson(cmd, *loadTarget, mVal, &prefabDir);

                    if (!isInstanced && target)
                        loadedSharedMaterials.insert(target);
                }

                if (isInstanced && target)
                {
                    MaterialInstance *inst = CreateMaterialInstance(mesh);
                    if (inst)
                        inst->ApplyDiffFromResolved(tempMat);
                }
            }
        }

        if (cmd)
        {
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }

        const auto &nodesVal = d["nodes"];
        std::vector<NodeId *> nodeMap(nodesVal.Size(), nullptr);

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            const auto &nv = nodesVal[ni];
            const std::string name = nv.HasMember("name") && nv["name"].IsString() ? nv["name"].GetString() : "Prefab Node";
            NodeId *node = CreateNode(name);
            if (nv.HasMember("enabled") && nv["enabled"].IsBool())
                SetNodeEnabled(node, nv["enabled"].GetBool());
            if (nv.HasMember("local_matrix") && nv["local_matrix"].IsArray() && nv["local_matrix"].Size() == 16)
                SetLocalMatrix(node, ReadMat4(nv["local_matrix"]), false);

            if (nv.HasMember("mesh_refs") && nv["mesh_refs"].IsArray())
            {
                const auto &arr = nv["mesh_refs"];
                for (rapidjson::SizeType mi = 0; mi < arr.Size(); mi++)
                {
                    int savedIdx = arr[mi].GetInt();
                    int sceneIdx = (savedIdx >= 0 && savedIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                       ? savedMeshToSceneMesh[savedIdx]
                                       : -1;
                    if (sceneIdx >= 0 && sceneIdx < static_cast<int>(m_meshes.size()))
                        AddMeshRef(node, sceneIdx);
                }
            }
            else if (nv.HasMember("mesh"))
            {
                int savedMeshIdx = nv["mesh"].GetInt();
                int sceneMeshIdx = (savedMeshIdx >= 0 && savedMeshIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                       ? savedMeshToSceneMesh[savedMeshIdx]
                                       : -1;
                if (sceneMeshIdx >= 0 && sceneMeshIdx < static_cast<int>(m_meshes.size()))
                    SetMeshRef(node, sceneMeshIdx);
            }

            if (nv.HasMember("script") && nv["script"].IsString())
            {
                SetNodeScript(node, nv["script"].GetString());
                if (nv.HasMember("scriptRunMode") && nv["scriptRunMode"].IsInt())
                    SetNodeScriptRunMode(node, static_cast<ScriptRunMode>(nv["scriptRunMode"].GetInt()));
            }

            nodeMap[ni] = node;
        }

        int rootIdx = d.HasMember("root") && d["root"].IsInt() ? d["root"].GetInt() : 0;
        if (rootIdx < 0 || rootIdx >= static_cast<int>(nodeMap.size()) || !nodeMap[rootIdx])
            rootIdx = nodeMap.empty() ? -1 : 0;
        NodeId *instanceRoot = rootIdx >= 0 ? nodeMap[rootIdx] : nullptr;

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            const auto &nv = nodesVal[ni];
            NodeId *node = nodeMap[ni];
            if (!node)
                continue;

            bool hasSavedParent = false;
            if (nv.HasMember("parent") && nv["parent"].IsInt())
            {
                int parentIdx = nv["parent"].GetInt();
                if (parentIdx >= 0 && parentIdx < static_cast<int>(nodeMap.size()) && nodeMap[parentIdx])
                {
                    ReparentNode(node, nodeMap[parentIdx]);
                    hasSavedParent = true;
                }
                else if (parentIdx >= 0)
                {
                    PE_WARN("[Prefab] Node %u has invalid parent index %d in %s",
                            static_cast<uint32_t>(ni), parentIdx, file.string().c_str());
                    if (instanceRoot && node != instanceRoot)
                    {
                        ReparentNode(node, instanceRoot);
                        hasSavedParent = true;
                    }
                }
            }
            if (!hasSavedParent && parent)
                ReparentNode(node, parent);
        }

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            const auto &nv = nodesVal[ni];
            NodeId *node = nodeMap[ni];
            if (!node)
                continue;

            uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;
            uint32_t restoreFlags = flags & ~(Component_Mesh | Component_Script | Component_GpuPending);
            if (restoreFlags)
                AddComponentFlag(node, restoreFlags);
            RestorePrefabNode(*this, node, nv, &prefabDir);

            if ((flags & Component_Camera) && nv.HasMember("camera") && nv["camera"].IsObject())
            {
                const auto &cv = nv["camera"];
                Camera *cam = new Camera();
                cam->SetName(nv.HasMember("name") && nv["name"].IsString() ? nv["name"].GetString() : ("Camera_" + std::to_string(ID::NextID())));
                if (cv.HasMember("projection") && cv["projection"].IsString())
                {
                    std::string mode = cv["projection"].GetString();
                    cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                }
                else if (cv.HasMember("orthographic") && cv["orthographic"].IsBool())
                {
                    cam->SetOrthographic(cv["orthographic"].GetBool());
                }
                if (cv.HasMember("fovx"))
                    cam->SetFovx(cv["fovx"].GetFloat());
                if (cv.HasMember("orthographic_size"))
                    cam->SetOrthographicSize(cv["orthographic_size"].GetFloat());
                if (cv.HasMember("near_plane"))
                    cam->SetNearPlane(cv["near_plane"].GetFloat());
                if (cv.HasMember("far_plane"))
                    cam->SetFarPlane(cv["far_plane"].GetFloat());
                if (cv.HasMember("speed"))
                    cam->SetSpeed(cv["speed"].GetFloat());
                cam->SetNodeId(node);
                m_cameras.push_back(cam);
                if (m_nodeComponentCache[node->index].camera)
                    m_nodeComponentCache[node->index].camera->camera = cam;
                const mat4 &lm = GetLocalMatrix(node);
                cam->SetPosition(vec3(lm[3]));
                if (cv.HasMember("euler"))
                    cam->SetEuler(ReadVec3(cv["euler"]));
                else
                    cam->SetEuler(glm::eulerAngles(quat_cast(mat3(lm))));
            }

            if ((flags & Component_Light) && nv.HasMember("light") && nv["light"].IsObject())
            {
                const auto &lv = nv["light"];
                std::string ltype = lv.HasMember("type") && lv["type"].IsString() ? lv["type"].GetString() : "";
                std::string lname = nv.HasMember("name") && nv["name"].IsString() ? nv["name"].GetString() : "";
                if (ltype == "directional")
                {
                    SceneDirectionalLight l{};
                    l.color = lv.HasMember("color") ? ReadVec4(lv["color"], vec4(1.f)) : vec4(1.f);
                    l.name = lname.empty() ? ("Directional Light " + std::to_string(ID::NextID())) : lname;
                    l.nodeId = node;
                    m_directionalLights.push_back(l);
                }
                else if (ltype == "point")
                {
                    ScenePointLight l{};
                    l.color = lv.HasMember("color") ? ReadVec4(lv["color"], vec4(1.f)) : vec4(1.f);
                    l.position.w = lv.HasMember("radius") ? lv["radius"].GetFloat() : 10.f;
                    l.name = lname.empty() ? ("Point Light " + std::to_string(ID::NextID())) : lname;
                    l.nodeId = node;
                    m_pointLights.push_back(l);
                }
                else if (ltype == "spot")
                {
                    SceneSpotLight l{};
                    l.color = lv.HasMember("color") ? ReadVec4(lv["color"], vec4(1.f)) : vec4(1.f);
                    l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                    l.params = lv.HasMember("params") ? ReadVec4(lv["params"], vec4(15.f, 5.f, 0.f, 0.f)) : vec4(15.f, 5.f, 0.f, 0.f);
                    l.name = lname.empty() ? ("Spot Light " + std::to_string(ID::NextID())) : lname;
                    l.nodeId = node;
                    m_spotLights.push_back(l);
                }
                else if (ltype == "area")
                {
                    SceneAreaLight l{};
                    l.color = lv.HasMember("color") ? ReadVec4(lv["color"], vec4(1.f)) : vec4(1.f);
                    l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                    l.size = lv.HasMember("size") ? ReadVec4(lv["size"], vec4(2.f, 2.f, 0.f, 0.f)) : vec4(2.f, 2.f, 0.f, 0.f);
                    l.name = lname.empty() ? ("Area Light " + std::to_string(ID::NextID())) : lname;
                    l.nodeId = node;
                    m_areaLights.push_back(l);
                }
            }
        }

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestoreSkyboxNode(*this, nodeMap[ni], nodesVal[ni]);
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            RestoreTriggerZoneNode(*this, nodeMap[ni], nodesVal[ni]);
        }
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestoreVoxelWorldNode(*this, nodeMap[ni], nodesVal[ni]);
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestoreRuntimeUiNode(*this, nodeMap[ni], nodesVal[ni], &prefabDir);
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestoreSpriteNode(*this, nodeMap[ni], nodesVal[ni], &prefabDir);
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestoreSkinnedStrip2DNode(*this, nodeMap[ni], nodesVal[ni]);

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            const auto &nv = nodesVal[ni];
            NodeId *node = nodeMap[ni];
            if (!node || !nv.HasMember("physics"))
                continue;
            const auto &pv = nv["physics"];
            PhysicsBodyDesc desc;
            if (pv.HasMember("body_type"))
                desc.bodyType = static_cast<PhysicsBodyType>(pv["body_type"].GetInt());
            if (pv.HasMember("shape_type"))
                desc.shapeType = static_cast<PhysicsShapeType>(pv["shape_type"].GetInt());
            if (pv.HasMember("mass"))
                desc.mass = pv["mass"].GetFloat();
            if (pv.HasMember("friction"))
                desc.friction = pv["friction"].GetFloat();
            if (pv.HasMember("restitution"))
                desc.restitution = pv["restitution"].GetFloat();
            if (pv.HasMember("auto_fit"))
                desc.autoFitShape = pv["auto_fit"].GetBool();
            if (pv.HasMember("is_trigger"))
                desc.isTrigger = pv["is_trigger"].GetBool();
            if (pv.HasMember("box_half_extents"))
                desc.boxHalfExtents = ReadVec3(pv["box_half_extents"], desc.boxHalfExtents);
            if (pv.HasMember("sphere_radius"))
                desc.sphereRadius = pv["sphere_radius"].GetFloat();
            if (pv.HasMember("capsule_half_height"))
                desc.capsuleHalfHeight = pv["capsule_half_height"].GetFloat();
            if (pv.HasMember("capsule_radius"))
                desc.capsuleRadius = pv["capsule_radius"].GetFloat();
            AddScenePhysicsBody(*this, node, desc);
        }

#ifdef PE_PHYSICS2D
        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
            RestorePhysics2DNode(*this, nodeMap[ni], nodesVal[ni]);
#endif

        for (rapidjson::SizeType ni = 0; ni < nodesVal.Size(); ni++)
        {
            const auto &nv = nodesVal[ni];
            NodeId *node = nodeMap[ni];
            if (!node || !nv.HasMember("audio"))
                continue;
            const auto &av = nv["audio"];
            AudioSourceDesc desc;
            if (av.HasMember("file"))
                desc.filePath = av["file"].GetString();
            if (av.HasMember("volume"))
                desc.volume = av["volume"].GetFloat();
            if (av.HasMember("pitch"))
                desc.pitch = av["pitch"].GetFloat();
            if (av.HasMember("min_distance"))
                desc.minDistance = av["min_distance"].GetFloat();
            if (av.HasMember("max_distance"))
                desc.maxDistance = av["max_distance"].GetFloat();
            if (av.HasMember("loop"))
                desc.loop = av["loop"].GetBool();
            if (av.HasMember("spatial"))
                desc.spatial = av["spatial"].GetBool();
            if (av.HasMember("autoplay"))
                desc.autoplay = av["autoplay"].GetBool();
            AddSceneAudioSource(*this, node, desc);
        }

        if (instanceRoot)
            SetNodePrefabPath(instanceRoot, file.generic_string());

        for (NodeId *node : nodeMap)
        {
            if (!node)
                continue;
            if (!m_nodeComponentCache[node->index].meshRefs->meshRefs.empty())
                m_nodeRuntime[node->index].gpuPending = true;
            MarkNodeDirty(node);
        }
        UpdateNodeMatrices();

        m_geometryDirty = m_geometryDirty || !savedMeshToSceneMesh.empty();
        m_instancesDirty = true;
        m_materialDirty = true;
        m_texturesDirty = true;
        m_dirty = true;
        ResetSkeletonCache();

        for (NodeId *node : nodeMap)
        {
            if (node && NodeHasSkinnedMesh(node) && !GetAnimationClipsForNode(node).empty() && !GetSkinnedStrip2DState(node))
                PlaySceneAnimation(*this, node, 0, true);
        }

        Log::Info("Prefab instantiated from: " + file.string());
        return instanceRoot ? MakeHandle(instanceRoot) : SceneNodeHandle{};
    }

    std::string Scene::TakeSnapshot() const
    {
        rapidjson::Document d;
        d.SetObject();
        SceneSerializationHelper serializer(*this, d);
        serializer.BuildLiveGeometryRemap();
        serializer.AddSources(nullptr);
        serializer.AddMeshes(nullptr);
        serializer.AddNodes();
        serializer.AddLegacyLights();
        serializer.AddLegacyCameras(SceneSerializationHelper::LegacyCameraMode::Snapshot);
        serializer.AddActiveCamera();
        serializer.AddEmitters();
        serializer.AddSettings();
        serializer.AddSceneScripts();

        // Compact JSON for minimal memory usage
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        writer.SetMaxDecimalPlaces(6);
        d.Accept(writer);
        return std::string(sb.GetString(), sb.GetSize());
    }

    bool Scene::RestoreSnapshot(const std::string &json)
    {
        rapidjson::Document d;
        d.Parse(json.c_str(), json.size());
        if (d.HasParseError() || !d.IsObject())
        {
            PE_WARN("[Scene] RestoreSnapshot skipped invalid JSON snapshot");
            return false;
        }
        if (!d.HasMember("sources") || !d["sources"].IsArray() ||
            !d.HasMember("nodes") || !d["nodes"].IsArray() ||
            (d.HasMember("meshes") && !d["meshes"].IsArray()) ||
            (d.HasMember("lights") && !d["lights"].IsArray()) ||
            (d.HasMember("cameras") && !d["cameras"].IsArray()) ||
            (d.HasMember("emitters") && !d["emitters"].IsArray()))
        {
            PE_WARN("[Scene] RestoreSnapshot skipped malformed scene snapshot");
            return false;
        }

        SyncSceneBeforeMutation();
        ClearSceneSelection();

        PE_INFO("[Scene] RestoreSnapshot begin: bytes=%zu currentNodes=%u currentMeshes=%zu currentSources=%zu",
                json.size(), GetNodeCount(), m_meshes.size(), m_sources.size());

        auto ReadVec3 = [](const rapidjson::Value &arr)
        { return vec3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat()); };
        auto ReadVec4 = [](const rapidjson::Value &arr)
        { return vec4(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat()); };
        auto ReadMat4 = [](const rapidjson::Value &arr)
        {
            mat4 m;
            float *p = value_ptr(m);
            for (int i = 0; i < 16; i++)
                p[i] = arr[i].GetFloat();
            return m;
        };

        bool hadEmbeddedCamerasSnap = false;
        bool hadEmbeddedLightsSnap = false;
        bool replayDefaultAnimations = false;
        bool geometryMatch = false;
        int replayedAnimationNodes = 0;
        // Restore scene graph (flat format from TakeSnapshot)
        if (d.HasMember("sources") && d.HasMember("nodes"))
        {
            const auto &snapshotSources = d["sources"];
            const auto &snapshotNodes = d["nodes"];
            uint32_t snapshotNodeCount = snapshotNodes.Size();
            uint32_t snapshotMeshCount = d.HasMember("meshes") ? d["meshes"].Size() : 0;

            auto restoreActiveEmbeddedCamera = [&](auto getNodeForSnapshotIndex)
            {
                if (!hadEmbeddedCamerasSnap || !d.HasMember("active_camera") || !d["active_camera"].IsUint() ||
                    !d.HasMember("cameras") || !d["cameras"].IsArray())
                    return;

                const uint32_t activeIndex = d["active_camera"].GetUint();
                const auto &cams = d["cameras"];
                if (activeIndex >= cams.Size())
                    return;

                const auto &cv = cams[activeIndex];
                Camera *active = nullptr;
                if (cv.HasMember("node_index") && cv["node_index"].IsInt())
                {
                    const int nodeIndex = cv["node_index"].GetInt();
                    if (nodeIndex >= 0)
                    {
                        if (NodeId *node = getNodeForSnapshotIndex(static_cast<uint32_t>(nodeIndex)))
                            active = GetCameraForNode(node);
                    }
                }

                if (!active && cv.HasMember("name") && cv["name"].IsString())
                {
                    const std::string name = cv["name"].GetString();
                    for (Camera *camera : m_cameras)
                    {
                        if (camera && camera->GetName() == name)
                        {
                            active = camera;
                            break;
                        }
                    }
                }

                if (active)
                    SetActiveCamera(active);
            };

            // Check if geometry is unchanged (same sources in same order, same counts)
            bool sourcesMatch = (snapshotSources.Size() == static_cast<rapidjson::SizeType>(m_sources.size()));
            if (sourcesMatch)
            {
                auto GetSourceId = [](const SceneSource &s) -> std::string
                {
                    if (!s.primitiveType.empty())
                        return MakePrimitiveSourceId(s.primitiveType, s.primitiveParams, s.primitiveParamCount);
                    auto u8 = s.filePath.u8string();
                    return "file:" + std::string(u8.begin(), u8.end());
                };
                auto GetSnapshotSourceId = [](const rapidjson::Value &sv) -> std::string
                {
                    if (sv.HasMember("primitive_type"))
                    {
                        uint32_t paramCount = 0;
                        vec4 params = ReadPrimitiveParams(sv, paramCount);
                        return MakePrimitiveSourceId(sv["primitive_type"].GetString(), params, paramCount);
                    }
                    if (sv.HasMember("path"))
                        return "file:" + std::string(sv["path"].GetString());
                    return "";
                };
                for (rapidjson::SizeType i = 0; i < snapshotSources.Size(); i++)
                {
                    if (GetSourceId(m_sources[i]) != GetSnapshotSourceId(snapshotSources[i]))
                    {
                        sourcesMatch = false;
                        break;
                    }
                }
            }

            geometryMatch = sourcesMatch &&
                            snapshotNodeCount == GetNodeCount() &&
                            snapshotMeshCount == static_cast<uint32_t>(m_meshes.size());

            PE_INFO("[Scene] RestoreSnapshot path: geometryMatch=%d snapshotNodes=%u snapshotMeshes=%u snapshotSources=%u currentNodes=%u currentMeshes=%zu currentSources=%zu",
                    geometryMatch ? 1 : 0,
                    snapshotNodeCount,
                    snapshotMeshCount,
                    static_cast<uint32_t>(snapshotSources.Size()),
                    GetNodeCount(),
                    m_meshes.size(),
                    m_sources.size());

            if (geometryMatch)
            {
                // Fast path: update SoA in-place (no geometry reload)

                // Null out camera/light nodeIds before re-wiring
                for (auto *cam : m_cameras)
                    if (cam)
                        cam->SetNodeId(nullptr);
                for (auto &l : m_directionalLights)
                    l.nodeId = nullptr;
                for (auto &l : m_pointLights)
                    l.nodeId = nullptr;
                for (auto &l : m_spotLights)
                    l.nodeId = nullptr;
                for (auto &l : m_areaLights)
                    l.nodeId = nullptr;
                m_directionalLights.clear();
                m_pointLights.clear();
                m_spotLights.clear();
                m_areaLights.clear();

                // Update nodes
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];

                    if (nv.HasMember("name"))
                        SetNodeName(node, nv["name"].GetString());
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        NodeId *newParent = (parentIdx >= 0 && parentIdx < static_cast<int>(snapshotNodeCount))
                                                ? m_nodeIds[parentIdx]
                                                : nullptr;
                        if (GetParent(node) != newParent)
                            ReparentNode(node, newParent);
                    }
                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]));
                    SetNodeEnabled(node, !nv.HasMember("enabled") || !nv["enabled"].IsBool() || nv["enabled"].GetBool());
                    SetNodeRenderVisible(node, true);
                    SetMeshRef(node, -1);
                    if (nv.HasMember("mesh_refs"))
                    {
                        const auto &arr = nv["mesh_refs"];
                        for (rapidjson::SizeType mi = 0; mi < arr.Size(); mi++)
                        {
                            int meshIdx = arr[mi].GetInt();
                            if (meshIdx >= 0 && meshIdx < static_cast<int>(m_meshes.size()))
                                AddMeshRef(node, meshIdx);
                        }
                    }
                    else if (nv.HasMember("mesh"))
                    {
                        int meshIdx = nv["mesh"].GetInt();
                        if (meshIdx >= 0 && meshIdx < static_cast<int>(m_meshes.size()))
                            SetMeshRef(node, meshIdx);
                    }
                    if (nv.HasMember("script"))
                    {
                        SetNodeScript(node, nv["script"].GetString());
                        if (nv.HasMember("scriptRunMode") && nv["scriptRunMode"].IsInt())
                            SetNodeScriptRunMode(node, static_cast<ScriptRunMode>(nv["scriptRunMode"].GetInt()));
                    }
                    else
                        SetNodeScript(node, "");

                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;
                    // Clear subsystem tags before restoring — Mesh/Script are already set by SetMeshRef/SetNodeScript above
                    RemoveComponentFlag(node, Component_Camera | Component_Light | Component_Physics | Component_Physics2D | Component_Audio | Component_Skybox | Component_RuntimeUi | Component_Prefab | Component_Sprite | Component_SceneSettings | Component_TriggerZone);
                    uint32_t restoreFlags = flags & ~(Component_Mesh | Component_Script | Component_GpuPending);
                    if (restoreFlags)
                        AddComponentFlag(node, restoreFlags);
                    RestorePrefabNode(*this, node, nv, nullptr);
                }

                // Update meshes (materials only)
                if (d.HasMember("meshes"))
                {
                    // Clear all existing instances before restoring
                    for (auto &m : m_meshes)
                        m.materialInstance = nullptr;
                    m_ownedMaterialInstances.clear();

                    const auto &meshesVal = d["meshes"];
                    std::unordered_set<Material *> loadedSharedMaterials;
                    Queue *queue = RHII.GetMainQueue();
                    CommandBuffer *cmd = queue ? queue->AcquireCommandBuffer() : nullptr;
                    if (cmd)
                        cmd->Begin();

                    for (rapidjson::SizeType mi = 0; mi < meshesVal.Size() && mi < static_cast<rapidjson::SizeType>(m_meshes.size()); mi++)
                    {
                        const auto &mVal = meshesVal[mi];
                        Mesh &mesh = m_meshes[mi];

                        if (mVal.HasMember("render_type"))
                            mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                        if (mVal.HasMember("lod_shift"))
                            mesh.lodShift = mVal["lod_shift"].GetUint();
                        if (mVal.HasMember("lod_enabled"))
                            mesh.lodEnabled = mVal["lod_enabled"].GetBool();
                        if (mVal.HasMember("lod_bias"))
                            mesh.lodBias = mVal["lod_bias"].GetFloat();

                        bool isInstanced = mVal.HasMember("instanced") && mVal["instanced"].GetBool();
                        Material *target = mesh.material;
                        Material tempMat;
                        if (isInstanced && target)
                            tempMat = *target;

                        Material *loadTarget = isInstanced ? &tempMat : target;
                        bool skipShared = !isInstanced && target && loadedSharedMaterials.count(target) > 0;

                        if (loadTarget && !skipShared)
                        {
                            loadTarget->renderType = mesh.renderType;
                            if (mVal.HasMember("double_sided"))
                                loadTarget->doubleSided = mVal["double_sided"].GetBool();
                            if (mVal.HasMember("texture_mask"))
                                loadTarget->textureMask = mVal["texture_mask"].GetUint();
                            if (mVal.HasMember("material_factors"))
                            {
                                mat4 factors[2];
                                factors[0] = ReadMat4(mVal["material_factors"][0]);
                                factors[1] = ReadMat4(mVal["material_factors"][1]);
                                DecodeMaterialFactorsFromPersistence(mesh.renderType, factors[0], factors[1]);
                                loadTarget->UnpackLegacyFactors(factors, loadTarget->textureMask);
                            }
                            RestoreMaterialTexturesFromJson(cmd, *loadTarget, mVal, nullptr);

                            if (!isInstanced && target)
                                loadedSharedMaterials.insert(target);
                        }

                        if (isInstanced && target)
                        {
                            MaterialInstance *inst = CreateMaterialInstance(mesh);
                            if (inst)
                                inst->ApplyDiffFromResolved(tempMat);
                        }
                    }

                    if (cmd)
                    {
                        cmd->End();
                        queue->Submit(1, &cmd, nullptr, nullptr);
                        cmd->Wait();
                        queue->ReturnCommandBuffer(cmd);
                    }
                }

                // Pass 3: re-wire cameras/lights from embedded node data
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];
                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;

                    if ((flags & Component_Camera) && nv.HasMember("camera"))
                    {
                        Camera *cam = GetCameraForNode(node);
                        if (!cam)
                        {
                            // Find first camera without nodeId
                            for (auto *c : m_cameras)
                                if (c && !c->GetNodeId())
                                {
                                    cam = c;
                                    break;
                                }
                        }
                        if (cam)
                        {
                            if (nv.HasMember("name"))
                                cam->SetName(nv["name"].GetString());
                            cam->SetNodeId(node);
                            // Ensure camera tag exists and store pointer
                            if (!m_nodeComponentCache[node->index].camera)
                                AddComponentFlag(node, Component_Camera);
                            m_nodeComponentCache[node->index].camera->camera = cam;
                            const auto &cv = nv["camera"];
                            if (cv.HasMember("projection") && cv["projection"].IsString())
                            {
                                std::string mode = cv["projection"].GetString();
                                cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                            }
                            else if (cv.HasMember("orthographic") && cv["orthographic"].IsBool())
                            {
                                cam->SetOrthographic(cv["orthographic"].GetBool());
                            }
                            if (cv.HasMember("fovx"))
                                cam->SetFovx(cv["fovx"].GetFloat());
                            if (cv.HasMember("orthographic_size"))
                                cam->SetOrthographicSize(cv["orthographic_size"].GetFloat());
                            if (cv.HasMember("near_plane"))
                                cam->SetNearPlane(cv["near_plane"].GetFloat());
                            if (cv.HasMember("far_plane"))
                                cam->SetFarPlane(cv["far_plane"].GetFloat());
                            if (cv.HasMember("speed"))
                                cam->SetSpeed(cv["speed"].GetFloat());
                            const mat4 &lm = GetLocalMatrix(node);
                            cam->SetPosition(vec3(lm[3]));
                            if (cv.HasMember("euler"))
                                cam->SetEuler(ReadVec3(cv["euler"]));
                            else
                                cam->SetEuler(glm::eulerAngles(quat_cast(mat3(lm))));
                            hadEmbeddedCamerasSnap = true;
                        }
                    }

                    if ((flags & Component_Light) && nv.HasMember("light"))
                    {
                        const auto &lv = nv["light"];
                        std::string ltype = lv.HasMember("type") ? lv["type"].GetString() : "";
                        std::string lname = nv.HasMember("name") ? nv["name"].GetString() : "";
                        if (ltype == "directional")
                        {
                            SceneDirectionalLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_directionalLights.push_back(l);
                        }
                        else if (ltype == "point")
                        {
                            ScenePointLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("radius") ? lv["radius"].GetFloat() : 10.f;
                            l.name = lname;
                            l.nodeId = node;
                            m_pointLights.push_back(l);
                        }
                        else if (ltype == "spot")
                        {
                            SceneSpotLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.params = lv.HasMember("params") ? ReadVec4(lv["params"]) : vec4(15.f, 5.f, 0.f, 0.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_spotLights.push_back(l);
                        }
                        else if (ltype == "area")
                        {
                            SceneAreaLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.size = lv.HasMember("size") ? ReadVec4(lv["size"]) : vec4(2.f, 2.f, 0.f, 0.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_areaLights.push_back(l);
                        }
                        hadEmbeddedLightsSnap = true;
                    }
                }

                restoreActiveEmbeddedCamera([&](uint32_t nodeIndex) -> NodeId *
                                            { return nodeIndex < snapshotNodeCount ? m_nodeIds[nodeIndex] : nullptr; });

                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSkyboxNode(*this, m_nodeIds[ni], snapshotNodes[ni]);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    RestoreTriggerZoneNode(*this, m_nodeIds[ni], snapshotNodes[ni]);
                }
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreVoxelWorldNode(*this, m_nodeIds[ni], snapshotNodes[ni]);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreRuntimeUiNode(*this, m_nodeIds[ni], snapshotNodes[ni], nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSpriteNode(*this, m_nodeIds[ni], snapshotNodes[ni], nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSkinnedStrip2DNode(*this, m_nodeIds[ni], snapshotNodes[ni]);

                // Restore physics bodies from embedded node data
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];
                    if (nv.HasMember("physics"))
                    {
                        const auto &pv = nv["physics"];
                        PhysicsBodyDesc desc;
                        if (pv.HasMember("body_type"))
                            desc.bodyType = static_cast<PhysicsBodyType>(pv["body_type"].GetInt());
                        if (pv.HasMember("shape_type"))
                            desc.shapeType = static_cast<PhysicsShapeType>(pv["shape_type"].GetInt());
                        if (pv.HasMember("mass"))
                            desc.mass = pv["mass"].GetFloat();
                        if (pv.HasMember("friction"))
                            desc.friction = pv["friction"].GetFloat();
                        if (pv.HasMember("restitution"))
                            desc.restitution = pv["restitution"].GetFloat();
                        if (pv.HasMember("auto_fit"))
                            desc.autoFitShape = pv["auto_fit"].GetBool();
                        if (pv.HasMember("is_trigger"))
                            desc.isTrigger = pv["is_trigger"].GetBool();
                        if (pv.HasMember("box_half_extents"))
                            desc.boxHalfExtents = ReadVec3(pv["box_half_extents"]);
                        if (pv.HasMember("sphere_radius"))
                            desc.sphereRadius = pv["sphere_radius"].GetFloat();
                        if (pv.HasMember("capsule_half_height"))
                            desc.capsuleHalfHeight = pv["capsule_half_height"].GetFloat();
                        if (pv.HasMember("capsule_radius"))
                            desc.capsuleRadius = pv["capsule_radius"].GetFloat();
                        if (!HasScenePhysicsBody(node))
                            AddScenePhysicsBody(*this, node, desc);
                        else if (auto *existing = GetScenePhysicsBodyDesc(node))
                            *existing = desc;
                    }
                    else if (HasScenePhysicsBody(node))
                    {
                        RemoveScenePhysicsBody(node);
                    }
                }

#ifdef PE_PHYSICS2D
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];
                    if (nv.HasMember("physics2d") && nv["physics2d"].IsObject())
                    {
                        Physics2DBodyDesc desc = ReadPhysics2DDesc(nv["physics2d"]);
                        AddScenePhysics2DBody(*this, node, desc);
                    }
                    else if (HasScenePhysics2DBody(node))
                    {
                        RemoveScenePhysics2DBody(node);
                    }
                }
#endif

                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];
                    if (nv.HasMember("audio"))
                    {
                        const auto &av = nv["audio"];
                        AudioSourceDesc desc;
                        if (av.HasMember("file"))
                            desc.filePath = av["file"].GetString();
                        if (av.HasMember("volume"))
                            desc.volume = av["volume"].GetFloat();
                        if (av.HasMember("pitch"))
                            desc.pitch = av["pitch"].GetFloat();
                        if (av.HasMember("min_distance"))
                            desc.minDistance = av["min_distance"].GetFloat();
                        if (av.HasMember("max_distance"))
                            desc.maxDistance = av["max_distance"].GetFloat();
                        if (av.HasMember("loop"))
                            desc.loop = av["loop"].GetBool();
                        if (av.HasMember("spatial"))
                            desc.spatial = av["spatial"].GetBool();
                        if (av.HasMember("autoplay"))
                            desc.autoplay = av["autoplay"].GetBool();
                        if (!HasSceneAudioSource(node))
                            AddSceneAudioSource(*this, node, desc);
                        else if (auto *existing = GetSceneAudioSourceDesc(node))
                            *existing = desc;
                    }
                    else if (HasSceneAudioSource(node))
                    {
                        RemoveSceneAudioSource(node);
                    }
                }

                // Mark all nodes dirty
                for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
                    MarkNodeDirty(m_nodeIds[ni]);
            }
            else
            {
                replayDefaultAnimations = true;
                m_generation++;

                // Slow path: geometry changed — clear everything and reload via LoadScene-style path
                ClearSceneSelection();

                // Null out camera/light nodeIds before freeing node memory
                for (auto *cam : m_cameras)
                    if (cam)
                        cam->SetNodeId(nullptr);
                for (auto &l : m_directionalLights)
                    l.nodeId = nullptr;
                for (auto &l : m_pointLights)
                    l.nodeId = nullptr;
                for (auto &l : m_spotLights)
                    l.nodeId = nullptr;
                for (auto &l : m_areaLights)
                    l.nodeId = nullptr;

                ClearScenePhysicsBodies();
#ifdef PE_PHYSICS2D
                ClearScenePhysics2DBodies();
#endif
                ClearSceneAudioSources();
                ClearSceneAnimations();

                // Clear old scene data synchronously (mirrors LoadSceneApply)
                DestroyAllNodeEntities();
                RetireAllNodeIds();

                m_nodeRuntime.clear();
                m_nodesMoved.clear();
                m_nodesDirty = false;

                m_meshes.clear();
                m_meshRuntimes.clear();
                m_ownedMaterialInstances.clear();
                m_ownedMaterials.clear();
                m_vertexStore.clear();
                m_positionUvStore.clear();
                m_aabbVertexStore.clear();
                m_indexStore.clear();
                m_imageStore.clear();
                m_samplerStore.clear();

                m_sources.clear();
                m_meshSourceInfos.clear();
                m_modelRootNodes.clear();
                m_primitiveGeometryCache.clear();
                ResetSkeletonCache();
                for (auto *model : m_models)
                    delete model;
                m_models.clear();

                // Reload sources
                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();

                std::vector<std::vector<int>> sourceMeshMaps(snapshotSources.Size());
                for (rapidjson::SizeType si = 0; si < snapshotSources.Size(); si++)
                {
                    const auto &sv = snapshotSources[si];
                    ModelAsset *model = nullptr;

                    if (sv.HasMember("primitive_type"))
                    {
                        uint32_t paramCount = 0;
                        vec4 params = ReadPrimitiveParams(sv, paramCount);
                        model = CreatePrimitiveModelFromSource(sv["primitive_type"].GetString(), params, paramCount);
                    }
                    else if (sv.HasMember("path"))
                    {
                        std::filesystem::path modelPath = sv["path"].GetString();
                        if (!std::filesystem::is_directory(modelPath))
                            model = ModelAsset::Load(modelPath);
                    }

                    if (model)
                    {
                        int sourceIndex = static_cast<int>(m_sources.size());
                        SceneSource source;
                        source.filePath = model->GetFilePath();
                        source.primitiveType = model->GetPrimitiveType();
                        source.primitiveParams = model->GetPrimitiveParams();
                        source.primitiveParamCount = model->GetPrimitiveParamCount();
                        source.modelId = model->GetId();
                        m_sources.push_back(std::move(source));

                        sourceMeshMaps[si] = AddModelGeometry(model, sourceIndex);
                        m_models.insert(model->GetId(), model);
                    }
                }

                // Apply mesh overrides
                std::vector<int> savedMeshToSceneMesh;
                if (d.HasMember("meshes"))
                {
                    const auto &meshesVal = d["meshes"];
                    savedMeshToSceneMesh.assign(meshesVal.Size(), -1);
                    std::unordered_set<Material *> loadedSharedMaterials;

                    for (rapidjson::SizeType mi = 0; mi < meshesVal.Size(); mi++)
                    {
                        const auto &mVal = meshesVal[mi];
                        int sceneMeshIdx = -1;
                        if (mVal.HasMember("source") && mVal.HasMember("source_mesh"))
                        {
                            int srcIdx = mVal["source"].GetInt();
                            int srcMesh = mVal["source_mesh"].GetInt();
                            if (srcIdx >= 0 && srcIdx < static_cast<int>(sourceMeshMaps.size()) &&
                                srcMesh >= 0 && srcMesh < static_cast<int>(sourceMeshMaps[srcIdx].size()))
                                sceneMeshIdx = sourceMeshMaps[srcIdx][srcMesh];
                        }
                        if (sceneMeshIdx < 0 || sceneMeshIdx >= static_cast<int>(m_meshes.size()))
                            continue;

                        savedMeshToSceneMesh[mi] = sceneMeshIdx;

                        Mesh &mesh = m_meshes[sceneMeshIdx];
                        if (mVal.HasMember("render_type"))
                            mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());
                        if (mVal.HasMember("lod_shift"))
                            mesh.lodShift = mVal["lod_shift"].GetUint();
                        if (mVal.HasMember("lod_enabled"))
                            mesh.lodEnabled = mVal["lod_enabled"].GetBool();
                        if (mVal.HasMember("lod_bias"))
                            mesh.lodBias = mVal["lod_bias"].GetFloat();

                        bool isInstanced = mVal.HasMember("instanced") && mVal["instanced"].GetBool();
                        Material *target = mesh.material;
                        Material tempMat;
                        if (isInstanced && target)
                            tempMat = *target;

                        Material *loadTarget = isInstanced ? &tempMat : target;
                        bool skipShared = !isInstanced && target && loadedSharedMaterials.count(target) > 0;

                        if (loadTarget && !skipShared)
                        {
                            loadTarget->renderType = mesh.renderType;
                            if (mVal.HasMember("double_sided"))
                                loadTarget->doubleSided = mVal["double_sided"].GetBool();
                            if (mVal.HasMember("texture_mask"))
                                loadTarget->textureMask = mVal["texture_mask"].GetUint();
                            if (mVal.HasMember("material_factors"))
                            {
                                mat4 factors[2];
                                factors[0] = ReadMat4(mVal["material_factors"][0]);
                                factors[1] = ReadMat4(mVal["material_factors"][1]);
                                DecodeMaterialFactorsFromPersistence(mesh.renderType, factors[0], factors[1]);
                                loadTarget->UnpackLegacyFactors(factors, loadTarget->textureMask);
                            }
                            RestoreMaterialTexturesFromJson(cmd, *loadTarget, mVal, nullptr);

                            if (!isInstanced && target)
                                loadedSharedMaterials.insert(target);
                        }

                        if (isInstanced && target)
                        {
                            MaterialInstance *inst = CreateMaterialInstance(mesh);
                            if (inst)
                                inst->ApplyDiffFromResolved(tempMat);
                        }
                    }
                }

                // Create nodes from flat array (two-pass)
                std::vector<NodeId *> nodeMap(snapshotNodeCount, nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    std::string name = nv.HasMember("name") ? nv["name"].GetString() : "";
                    NodeId *node = CreateNode(name);
                    if (nv.HasMember("enabled") && nv["enabled"].IsBool())
                        SetNodeEnabled(node, nv["enabled"].GetBool());
                    if (nv.HasMember("local_matrix"))
                        SetLocalMatrix(node, ReadMat4(nv["local_matrix"]), false);
                    if (nv.HasMember("mesh_refs"))
                    {
                        const auto &arr = nv["mesh_refs"];
                        for (rapidjson::SizeType mi = 0; mi < arr.Size(); mi++)
                        {
                            int savedIdx = arr[mi].GetInt();
                            int sceneIdx = (savedIdx >= 0 && savedIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                               ? savedMeshToSceneMesh[savedIdx]
                                               : -1;
                            if (sceneIdx >= 0 && sceneIdx < static_cast<int>(m_meshes.size()))
                                AddMeshRef(node, sceneIdx);
                        }
                    }
                    else if (nv.HasMember("mesh"))
                    {
                        int savedMeshIdx = nv["mesh"].GetInt();
                        int sceneMeshIdx = (savedMeshIdx >= 0 && savedMeshIdx < static_cast<int>(savedMeshToSceneMesh.size()))
                                               ? savedMeshToSceneMesh[savedMeshIdx]
                                               : -1;
                        if (sceneMeshIdx >= 0 && sceneMeshIdx < static_cast<int>(m_meshes.size()))
                            SetMeshRef(node, sceneMeshIdx);
                    }
                    if (nv.HasMember("script"))
                    {
                        SetNodeScript(node, nv["script"].GetString());
                        if (nv.HasMember("scriptRunMode") && nv["scriptRunMode"].IsInt())
                            SetNodeScriptRunMode(node, static_cast<ScriptRunMode>(nv["scriptRunMode"].GetInt()));
                    }
                    nodeMap[ni] = node;
                }
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    if (nv.HasMember("parent"))
                    {
                        int parentIdx = nv["parent"].GetInt();
                        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodeMap.size()) && nodeMap[parentIdx])
                            ReparentNode(nodeMap[ni], nodeMap[parentIdx]);
                    }
                }
                for (NodeId *node : nodeMap)
                {
                    if (node)
                        MarkNodeDirty(node);
                }
                UpdateNodeMatrices();

                // Pass 3: wire up cameras/lights from embedded node data
                m_directionalLights.clear();
                m_pointLights.clear();
                m_spotLights.clear();
                m_areaLights.clear();
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node)
                        continue;
                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;
                    uint32_t restoreFlags = flags & ~(Component_Mesh | Component_Script | Component_GpuPending);
                    if (restoreFlags)
                        AddComponentFlag(node, restoreFlags);
                    RestorePrefabNode(*this, node, nv, nullptr);

                    if ((flags & Component_Camera) && nv.HasMember("camera"))
                    {
                        Camera *cam = nullptr;
                        for (auto *c : m_cameras)
                            if (c && !c->GetNodeId())
                            {
                                cam = c;
                                break;
                            }
                        if (!cam)
                        {
                            cam = new Camera();
                            m_cameras.push_back(cam);
                        }
                        if (nv.HasMember("name"))
                            cam->SetName(nv["name"].GetString());
                        const auto &cv = nv["camera"];
                        if (cv.HasMember("projection") && cv["projection"].IsString())
                        {
                            std::string mode = cv["projection"].GetString();
                            cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                        }
                        else if (cv.HasMember("orthographic") && cv["orthographic"].IsBool())
                        {
                            cam->SetOrthographic(cv["orthographic"].GetBool());
                        }
                        if (cv.HasMember("fovx"))
                            cam->SetFovx(cv["fovx"].GetFloat());
                        if (cv.HasMember("orthographic_size"))
                            cam->SetOrthographicSize(cv["orthographic_size"].GetFloat());
                        if (cv.HasMember("near_plane"))
                            cam->SetNearPlane(cv["near_plane"].GetFloat());
                        if (cv.HasMember("far_plane"))
                            cam->SetFarPlane(cv["far_plane"].GetFloat());
                        if (cv.HasMember("speed"))
                            cam->SetSpeed(cv["speed"].GetFloat());
                        cam->SetNodeId(node);
                        if (m_nodeComponentCache[node->index].camera)
                            m_nodeComponentCache[node->index].camera->camera = cam;
                        const mat4 &lm = GetLocalMatrix(node);
                        cam->SetPosition(vec3(lm[3]));
                        if (cv.HasMember("euler"))
                            cam->SetEuler(ReadVec3(cv["euler"]));
                        else
                            cam->SetEuler(glm::eulerAngles(quat_cast(mat3(lm))));
                        hadEmbeddedCamerasSnap = true;
                    }
                    if ((flags & Component_Light) && nv.HasMember("light"))
                    {
                        const auto &lv = nv["light"];
                        std::string ltype = lv.HasMember("type") ? lv["type"].GetString() : "";
                        std::string lname = nv.HasMember("name") ? nv["name"].GetString() : "";
                        if (ltype == "directional")
                        {
                            SceneDirectionalLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_directionalLights.push_back(l);
                        }
                        else if (ltype == "point")
                        {
                            ScenePointLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("radius") ? lv["radius"].GetFloat() : 10.f;
                            l.name = lname;
                            l.nodeId = node;
                            m_pointLights.push_back(l);
                        }
                        else if (ltype == "spot")
                        {
                            SceneSpotLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.params = lv.HasMember("params") ? ReadVec4(lv["params"]) : vec4(15.f, 5.f, 0.f, 0.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_spotLights.push_back(l);
                        }
                        else if (ltype == "area")
                        {
                            SceneAreaLight l{};
                            l.color = lv.HasMember("color") ? ReadVec4(lv["color"]) : vec4(1.f);
                            l.position.w = lv.HasMember("range") ? lv["range"].GetFloat() : 10.f;
                            l.size = lv.HasMember("size") ? ReadVec4(lv["size"]) : vec4(2.f, 2.f, 0.f, 0.f);
                            l.name = lname;
                            l.nodeId = node;
                            m_areaLights.push_back(l);
                        }
                        hadEmbeddedLightsSnap = true;
                    }
                }

                restoreActiveEmbeddedCamera([&](uint32_t nodeIndex) -> NodeId *
                                            { return nodeIndex < nodeMap.size() ? nodeMap[nodeIndex] : nullptr; });

                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSkyboxNode(*this, nodeMap[ni], snapshotNodes[ni]);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    RestoreTriggerZoneNode(*this, nodeMap[ni], snapshotNodes[ni]);
                }
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreVoxelWorldNode(*this, nodeMap[ni], snapshotNodes[ni]);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreRuntimeUiNode(*this, nodeMap[ni], snapshotNodes[ni], nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSpriteNode(*this, nodeMap[ni], snapshotNodes[ni], nullptr);
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestoreSkinnedStrip2DNode(*this, nodeMap[ni], snapshotNodes[ni]);

                // Restore physics bodies from embedded node data (slow path)
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node)
                        continue;
                    if (nv.HasMember("physics"))
                    {
                        const auto &pv = nv["physics"];
                        PhysicsBodyDesc desc;
                        if (pv.HasMember("body_type"))
                            desc.bodyType = static_cast<PhysicsBodyType>(pv["body_type"].GetInt());
                        if (pv.HasMember("shape_type"))
                            desc.shapeType = static_cast<PhysicsShapeType>(pv["shape_type"].GetInt());
                        if (pv.HasMember("mass"))
                            desc.mass = pv["mass"].GetFloat();
                        if (pv.HasMember("friction"))
                            desc.friction = pv["friction"].GetFloat();
                        if (pv.HasMember("restitution"))
                            desc.restitution = pv["restitution"].GetFloat();
                        if (pv.HasMember("auto_fit"))
                            desc.autoFitShape = pv["auto_fit"].GetBool();
                        if (pv.HasMember("is_trigger"))
                            desc.isTrigger = pv["is_trigger"].GetBool();
                        if (pv.HasMember("box_half_extents"))
                            desc.boxHalfExtents = ReadVec3(pv["box_half_extents"]);
                        if (pv.HasMember("sphere_radius"))
                            desc.sphereRadius = pv["sphere_radius"].GetFloat();
                        if (pv.HasMember("capsule_half_height"))
                            desc.capsuleHalfHeight = pv["capsule_half_height"].GetFloat();
                        if (pv.HasMember("capsule_radius"))
                            desc.capsuleRadius = pv["capsule_radius"].GetFloat();
                        AddScenePhysicsBody(*this, node, desc);
                    }
                }

#ifdef PE_PHYSICS2D
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                    RestorePhysics2DNode(*this, nodeMap[ni], snapshotNodes[ni]);
#endif

                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = nodeMap[ni];
                    if (!node || !nv.HasMember("audio"))
                        continue;
                    const auto &av = nv["audio"];
                    AudioSourceDesc desc;
                    if (av.HasMember("file"))
                        desc.filePath = av["file"].GetString();
                    if (av.HasMember("volume"))
                        desc.volume = av["volume"].GetFloat();
                    if (av.HasMember("pitch"))
                        desc.pitch = av["pitch"].GetFloat();
                    if (av.HasMember("min_distance"))
                        desc.minDistance = av["min_distance"].GetFloat();
                    if (av.HasMember("max_distance"))
                        desc.maxDistance = av["max_distance"].GetFloat();
                    if (av.HasMember("loop"))
                        desc.loop = av["loop"].GetBool();
                    if (av.HasMember("spatial"))
                        desc.spatial = av["spatial"].GetBool();
                    if (av.HasMember("autoplay"))
                        desc.autoplay = av["autoplay"].GetBool();
                    AddSceneAudioSource(*this, node, desc);
                }

                UploadBuffers(cmd);

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);
            }
        }

        // 3. Restore lights (clear and recreate - cheap)
        if (!hadEmbeddedLightsSnap && d.HasMember("lights"))
        {
            m_directionalLights.clear();
            m_pointLights.clear();
            m_spotLights.clear();
            m_areaLights.clear();

            const auto &lights = d["lights"];
            for (const auto &lVal : lights.GetArray())
            {
                std::string type = lVal["type"].GetString();
                if (type == "directional")
                {
                    SceneDirectionalLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    if (lVal.HasMember("rotation"))
                        l.rotation = ReadVec4(lVal["rotation"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Directional Light " + std::to_string(ID::NextID());
                    m_directionalLights.push_back(l);
                }
                else if (type == "point")
                {
                    ScenePointLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Point Light " + std::to_string(ID::NextID());
                    m_pointLights.push_back(l);
                }
                else if (type == "spot")
                {
                    SceneSpotLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.params = ReadVec4(lVal["params"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Spot Light " + std::to_string(ID::NextID());
                    m_spotLights.push_back(l);
                }
                else if (type == "area")
                {
                    SceneAreaLight l{};
                    l.color = ReadVec4(lVal["color"]);
                    l.position = ReadVec4(lVal["position"]);
                    l.rotation = ReadVec4(lVal["rotation"]);
                    l.size = ReadVec4(lVal["size"]);
                    l.name = lVal.HasMember("name") ? lVal["name"].GetString() : "Area Light " + std::to_string(ID::NextID());
                    m_areaLights.push_back(l);
                }
            }
        }

        // 4. Restore cameras (skipped if cameras were loaded from embedded node data)
        if (!hadEmbeddedCamerasSnap && d.HasMember("cameras"))
        {
            const auto &cams = d["cameras"];
            for (rapidjson::SizeType i = 0; i < cams.Size(); i++)
            {
                Camera *cam = nullptr;
                if (i < m_cameras.size())
                    cam = m_cameras[i];
                else
                {
                    cam = new Camera();
                    cam->SetName("Camera_" + std::to_string(ID::NextID()));
                    m_cameras.push_back(cam);
                }

                const auto &cv = cams[i];
                if (cv.HasMember("name"))
                    cam->SetName(cv["name"].GetString());
                if (cv.HasMember("position"))
                    cam->SetPosition(ReadVec3(cv["position"]));
                if (cv.HasMember("euler"))
                    cam->SetEuler(ReadVec3(cv["euler"]));
                if (cv.HasMember("projection") && cv["projection"].IsString())
                {
                    std::string mode = cv["projection"].GetString();
                    cam->SetProjectionMode(mode == "orthographic" ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                }
                else if (cv.HasMember("orthographic") && cv["orthographic"].IsBool())
                {
                    cam->SetOrthographic(cv["orthographic"].GetBool());
                }
                if (cv.HasMember("fovx"))
                    cam->SetFovx(cv["fovx"].GetFloat());
                if (cv.HasMember("orthographic_size"))
                    cam->SetOrthographicSize(cv["orthographic_size"].GetFloat());
                if (cv.HasMember("near_plane"))
                    cam->SetNearPlane(cv["near_plane"].GetFloat());
                if (cv.HasMember("far_plane"))
                    cam->SetFarPlane(cv["far_plane"].GetFloat());
                if (cv.HasMember("speed"))
                    cam->SetSpeed(cv["speed"].GetFloat());
            }

            if (d.HasMember("active_camera"))
            {
                uint32_t idx = d["active_camera"].GetUint();
                if (idx < m_cameras.size())
                    SetActiveCamera(m_cameras[idx]);
            }
        }

        // 5. Restore particle emitters
        if (d.HasMember("emitters") && m_particleManager)
        {
            auto &emitters = m_particleManager->GetEmitters();
            auto &names = m_particleManager->GetEmitterNames();
            emitters.clear();
            names.clear();

            const auto &emArr = d["emitters"];
            for (const auto &ev : emArr.GetArray())
            {
                ParticleEmitter e{};
                if (ev.HasMember("position"))
                    e.position = ReadVec4(ev["position"]);
                if (ev.HasMember("velocity"))
                    e.velocity = ReadVec4(ev["velocity"]);
                if (ev.HasMember("color_start"))
                    e.colorStart = ReadVec4(ev["color_start"]);
                if (ev.HasMember("color_end"))
                    e.colorEnd = ReadVec4(ev["color_end"]);
                if (ev.HasMember("size_life"))
                    e.sizeLife = ReadVec4(ev["size_life"]);
                if (ev.HasMember("physics"))
                    e.physics = ReadVec4(ev["physics"]);
                if (ev.HasMember("gravity"))
                    e.gravity = ReadVec4(ev["gravity"]);
                if (ev.HasMember("animation"))
                    e.animation = ReadVec4(ev["animation"]);
                if (ev.HasMember("texture_index"))
                    e.textureIndex = ev["texture_index"].GetUint();
                if (ev.HasMember("count"))
                    e.count = ev["count"].GetUint();
                if (ev.HasMember("orientation"))
                    e.orientation = ev["orientation"].GetUint();
                emitters.push_back(e);
                names.push_back(ev.HasMember("name") ? ev["name"].GetString() : ("Emitter " + std::to_string(names.size())));
            }
            m_particleManager->UpdateEmitterBuffer();
        }

        // 6. Restore global settings
        if (d.HasMember("settings"))
        {
            const auto &settings = d["settings"];
            ApplySceneSettingsMembers(settings);
            EnsureSkyboxNodeFromSettings(false);
            MarkUniformsDirty();
        }

        ParseSceneScriptManifest(d, m_scriptManifest);

        // Update texture descriptors (needed when material texture masks change)
        UpdateTextures();

        // Update render pass descriptor sets (needed when shadow settings change)
        RefreshSceneRenderDescriptors();

        if (replayDefaultAnimations)
        {
            for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
            {
                NodeId *node = m_nodeIds[ni];
                if (NodeHasSkinnedMesh(node) && !GetAnimationClipsForNode(node).empty() && !GetSkinnedStrip2DState(node))
                {
                    PlaySceneAnimation(*this, node, 0, true);
                    ++replayedAnimationNodes;
                }
            }
        }

        UpdateCameras(false);
        UpdateNodeMatrices();
        UpdateLights();

        const SkinnedAnimationStats restoredAnimationStats = GetSkinnedAnimationStats(*this);
        PE_INFO("[Scene] RestoreSnapshot end: geometryMatch=%d nodes=%u meshes=%zu skinnedNodes=%d boneRefs=%zu clipRefs=%zu replayed=%d",
                geometryMatch ? 1 : 0,
                GetNodeCount(),
                m_meshes.size(),
                restoredAnimationStats.nodes,
                restoredAnimationStats.boneRefs,
                restoredAnimationStats.clipRefs,
                replayedAnimationNodes);
        return true;
    }
} // namespace pe
