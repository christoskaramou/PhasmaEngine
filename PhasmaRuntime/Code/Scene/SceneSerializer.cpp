#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/SceneRuntimeHooks.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
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
            if (ptype == "cube")
                return Primitives::CreateCube(paramCount >= 1 ? params.x : 1.0f);
            if (ptype == "sphere")
                return Primitives::CreateSphere(paramCount >= 1 ? params.x : 1.0f);
            if (ptype == "plane")
                return Primitives::CreatePlane(paramCount >= 1 ? params.x : 10.0f, paramCount >= 2 ? params.y : 10.0f);
            if (ptype == "cylinder")
                return Primitives::CreateCylinder(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 2.0f);
            if (ptype == "cone")
                return Primitives::CreateCone(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 2.0f);
            if (ptype == "quad")
                return Primitives::CreateQuad(paramCount >= 1 ? params.x : 1.0f, paramCount >= 2 ? params.y : 1.0f);
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

        void AddGlobalSettingsMembers(rapidjson::Value &settings, rapidjson::Document::AllocatorType &allocator)
        {
            auto &gSettings = Settings::Get<GlobalSettings>();
            settings.AddMember("right_handed", gSettings.right_handed, allocator);
            settings.AddMember("reverse_depth", gSettings.reverse_depth, allocator);
            settings.AddMember("frustum_culling", gSettings.frustum_culling, allocator);
            settings.AddMember("shadows", gSettings.shadows, allocator);
            settings.AddMember("shadow_map_size", gSettings.shadow_map_size, allocator);
            settings.AddMember("num_cascades", gSettings.num_cascades, allocator);
            settings.AddMember("shadow_distance", gSettings.shadow_distance, allocator);
            settings.AddMember("shadow_cascade_lambda", gSettings.shadow_cascade_lambda, allocator);
            settings.AddMember("shadow_normal_bias", gSettings.shadow_normal_bias, allocator);
            settings.AddMember("shadow_fade_fraction", gSettings.shadow_fade_fraction, allocator);
            settings.AddMember("shadow_filter_radius", gSettings.shadow_filter_radius, allocator);
            settings.AddMember("shadow_debug_mode", gSettings.shadow_debug_mode, allocator);
            settings.AddMember("render_scale", gSettings.render_scale, allocator);
            settings.AddMember("ssao", gSettings.ssao, allocator);
            settings.AddMember("fxaa", gSettings.fxaa, allocator);
            settings.AddMember("taa", gSettings.taa, allocator);
            settings.AddMember("cas_sharpening", gSettings.cas_sharpening, allocator);
            settings.AddMember("cas_sharpness", gSettings.cas_sharpness, allocator);
            settings.AddMember("ssr", gSettings.ssr, allocator);
            settings.AddMember("tonemapping", gSettings.tonemapping, allocator);
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
            settings.AddMember("day", gSettings.day, allocator);

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
            settings.AddMember("dynamic_rendering", gSettings.dynamic_rendering, allocator);
            settings.AddMember("ray_tracing_support", gSettings.ray_tracing_support, allocator);
            settings.AddMember("render_mode", static_cast<int>(gSettings.render_mode), allocator);
            settings.AddMember("use_Disney_PBR", gSettings.use_Disney_PBR, allocator);
            settings.AddMember("present_mode", static_cast<int>(gSettings.preferred_present_mode), allocator);
        }

        void ApplyGlobalSettingsMembers(const rapidjson::Value &settings)
        {
            auto &gSettings = Settings::Get<GlobalSettings>();
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
                gSettings.render_scale = settings["render_scale"].GetFloat();
                EventSystem::PushEvent(EventType::Resize);
            }
            if (settings.HasMember("ssao"))
                gSettings.ssao = settings["ssao"].GetBool();
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
            if (settings.HasMember("day"))
                gSettings.day = settings["day"].GetBool();
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
            if (settings.HasMember("dynamic_rendering"))
                gSettings.dynamic_rendering = settings["dynamic_rendering"].GetBool() && RHII.GetCaps().dynamicRendering;
            if (settings.HasMember("ray_tracing_support"))
                gSettings.ray_tracing_support = RHII.GetCaps().rayTracing;
            if (settings.HasMember("render_mode"))
                gSettings.render_mode = ClampRenderModeToRayTracingSupport(
                    static_cast<RenderMode>(settings["render_mode"].GetInt()), RHII.GetCaps().rayTracing);
            if (settings.HasMember("use_Disney_PBR"))
                gSettings.use_Disney_PBR = settings["use_Disney_PBR"].GetBool();
            if (settings.HasMember("present_mode"))
            {
                gSettings.preferred_present_mode = static_cast<PePresentMode>(settings["present_mode"].GetInt());
                EventSystem::PushEvent(EventType::PresentMode);
            }
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

        int CountSkinnedAnimationNodes(const Scene &scene)
        {
            int count = 0;
            for (uint32_t ni = 0; ni < scene.GetNodeCount(); ++ni)
            {
                const NodeId *node = scene.GetNodeId(ni);
                if (node && scene.NodeHasSkinnedMesh(node))
                    ++count;
            }

            return count;
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

        void AddSettings()
        {
            rapidjson::Value settings(rapidjson::kObjectType);
            AddGlobalSettingsMembers(settings, allocator);

            document.AddMember("settings", settings.Move(), allocator);
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

        void AddNodes()
        {
            rapidjson::Value nodesArr(rapidjson::kArrayType);
            for (uint32_t ni = 0; ni < scene.GetNodeCount(); ni++)
            {
                NodeId *node = scene.m_nodeIds[ni];
                rapidjson::Value nodeObj(rapidjson::kObjectType);
                const auto &cache = scene.m_nodeComponentCache[ni];
                nodeObj.AddMember("name", MakeStringValue(cache.name->name), allocator);

                NodeId *parentNode = cache.hierarchy->parent;
                int parentIdx = parentNode ? static_cast<int>(parentNode->index) : -1;
                nodeObj.AddMember("parent", parentIdx, allocator);

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
                    nodeObj.AddMember("script", MakeStringValue(cache.script->path), allocator);

                uint32_t flags = scene.GetComponentFlags(scene.m_nodeIds[ni]) & ~Component_GpuPending;
                if (flags)
                    nodeObj.AddMember("component_flags", flags, allocator);

                if ((flags & Component_Camera) && !scene.m_cameras.empty())
                {
                    Camera *cam = scene.GetCameraForNode(node);
                    if (cam)
                    {
                        rapidjson::Value camObj(rapidjson::kObjectType);
                        camObj.AddMember("fovx", cam->Fovx(), allocator);
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
                        rapidjson::Value halfExtents;
                        SetVec3(halfExtents, desc->boxHalfExtents);
                        phys.AddMember("box_half_extents", halfExtents.Move(), allocator);
                        phys.AddMember("sphere_radius", desc->sphereRadius, allocator);
                        phys.AddMember("capsule_half_height", desc->capsuleHalfHeight, allocator);
                        phys.AddMember("capsule_radius", desc->capsuleRadius, allocator);
                        nodeObj.AddMember("physics", phys.Move(), allocator);
                    }
                }

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
                camObj.AddMember("near_plane", camera->GetNearPlane(), allocator);
                camObj.AddMember("far_plane", camera->GetFarPlane(), allocator);
                camObj.AddMember("speed", camera->GetSpeed(), allocator);

                if (mode == LegacyCameraMode::SaveFile)
                {
                    NodeId *camNode = camera->GetNodeId();
                    if (camNode)
                        camObj.AddMember("node_index", static_cast<int>(camNode->index), allocator);
                }

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
        serializer.BuildLiveGeometryRemap();
        serializer.AddSources(&sceneDir);
        serializer.AddMeshes(&sceneDir);
        serializer.AddNodes();
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

    void Scene::NewScene()
    {
        m_generation++;
        m_scenePath.clear();
        m_dirty = false;

        if (IsScenePhysicsSimulating())
            StopScenePhysicsSimulation();
        ClearScenePhysicsBodies();

        ClearSceneSelection();
        ClearSceneAnimations();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();

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

        // Free all NodeId allocations and clear SoA stores
        for (NodeId *id : m_nodeIds)
            delete id;
        for (NodeId *id : m_freeNodeIds)
            delete id;
        m_nodeIds.clear();
        m_freeNodeIds.clear();

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
        auto &loading = Settings::Get<GlobalSettings>().loading;
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

        // Clear all physics bodies before freeing node memory
        if (IsScenePhysicsSimulating())
            StopScenePhysicsSimulation();
        ClearScenePhysicsBodies();
        ClearSceneAudioSources();
        ClearSceneAnimations();

        // Clear existing scene: free SoA data, delete models, clear geometry stores
        ClearSceneSelection();

        m_sources.clear();
        m_meshSourceInfos.clear();
        m_modelRootNodes.clear();

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

        for (NodeId *id : m_nodeIds)
            delete id;
        for (NodeId *id : m_freeNodeIds)
            delete id;
        m_nodeIds.clear();
        m_freeNodeIds.clear();

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
            ApplyGlobalSettingsMembers(settings);
            MarkUniformsDirty();
        }

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
                        SetNodeScript(node, nv["script"].GetString());

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
                        if (cv.HasMember("fovx"))
                            cam->SetFovx(cv["fovx"].GetFloat());
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
                if (cVal.HasMember("fovx"))
                    cam->SetFovx(cVal["fovx"].GetFloat());
                if (cVal.HasMember("near_plane"))
                    cam->SetNearPlane(cVal["near_plane"].GetFloat());
                if (cVal.HasMember("far_plane"))
                    cam->SetFarPlane(cVal["far_plane"].GetFloat());
                if (cVal.HasMember("speed"))
                    cam->SetSpeed(cVal["speed"].GetFloat());
            }
        }

        if (d.HasMember("active_camera"))
        {
            uint32_t activeIndex = d["active_camera"].GetUint();
            if (activeIndex < m_cameras.size())
                SetActiveCamera(m_cameras[activeIndex]);
        }

        m_autoplayAnimations = true;

        if (!GetAnimationClips().empty())
        {
            for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
            {
                NodeId *node = m_nodeIds[ni];
                if (NodeHasSkinnedMesh(node))
                    PlaySceneAnimation(*this, node, 0, true);
            }
        }

        Log::Info("Scene loaded from: " + preload.filePath.string());
    }

    void Scene::LoadScene(const std::filesystem::path &file)
    {
        LoadSceneApply(PreloadScene(file));
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

        // Compact JSON for minimal memory usage
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        writer.SetMaxDecimalPlaces(6);
        d.Accept(writer);
        return std::string(sb.GetString(), sb.GetSize());
    }

    void Scene::RestoreSnapshot(const std::string &json)
    {
        rapidjson::Document d;
        d.Parse(json.c_str(), json.size());
        if (d.HasParseError())
            return;

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
                        SetNodeScript(node, nv["script"].GetString());
                    else
                        SetNodeScript(node, "");

                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;
                    // Clear subsystem tags before restoring — Mesh/Script are already set by SetMeshRef/SetNodeScript above
                    RemoveComponentFlag(node, Component_Camera | Component_Light | Component_Physics | Component_Audio);
                    uint32_t restoreFlags = flags & ~(Component_Mesh | Component_Script | Component_GpuPending);
                    if (restoreFlags)
                        AddComponentFlag(node, restoreFlags);
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

                    for (rapidjson::SizeType mi = 0; mi < meshesVal.Size() && mi < static_cast<rapidjson::SizeType>(m_meshes.size()); mi++)
                    {
                        const auto &mVal = meshesVal[mi];
                        Mesh &mesh = m_meshes[mi];

                        if (mVal.HasMember("render_type"))
                            mesh.renderType = static_cast<RenderType>(mVal["render_type"].GetInt());

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

                // Pass 3: re-wire cameras/lights from embedded node data
                for (uint32_t ni = 0; ni < snapshotNodeCount; ni++)
                {
                    const auto &nv = snapshotNodes[ni];
                    NodeId *node = m_nodeIds[ni];
                    uint32_t flags = nv.HasMember("component_flags") ? nv["component_flags"].GetUint() : 0;

                    if ((flags & Component_Camera) && nv.HasMember("camera"))
                    {
                        Camera *cam = (ni < m_cameras.size()) ? m_cameras[ni] : nullptr;
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
                            cam->SetNodeId(node);
                            // Ensure camera tag exists and store pointer
                            if (!m_nodeComponentCache[node->index].camera)
                                AddComponentFlag(node, Component_Camera);
                            m_nodeComponentCache[node->index].camera->camera = cam;
                            const auto &cv = nv["camera"];
                            if (cv.HasMember("fovx"))
                                cam->SetFovx(cv["fovx"].GetFloat());
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
                ClearSceneAudioSources();
                ClearSceneAnimations();

                // Clear old scene data synchronously (mirrors LoadSceneApply)
                DestroyAllNodeEntities();
                for (NodeId *id : m_nodeIds)
                    delete id;
                for (NodeId *id : m_freeNodeIds)
                    delete id;
                m_nodeIds.clear();
                m_freeNodeIds.clear();

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
                        SetNodeScript(node, nv["script"].GetString());
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
                        const auto &cv = nv["camera"];
                        if (cv.HasMember("fovx"))
                            cam->SetFovx(cv["fovx"].GetFloat());
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
                if (cv.HasMember("fovx"))
                    cam->SetFovx(cv["fovx"].GetFloat());
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
            ApplyGlobalSettingsMembers(settings);
            MarkUniformsDirty();
        }

        // Update texture descriptors (needed when material texture masks change)
        UpdateTextures();

        // Update render pass descriptor sets (needed when shadows/day settings change)
        RefreshSceneRenderDescriptors();

        if (replayDefaultAnimations && !GetAnimationClips().empty())
        {
            for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
            {
                NodeId *node = m_nodeIds[ni];
                if (NodeHasSkinnedMesh(node))
                {
                    PlaySceneAnimation(*this, node, 0, true);
                    ++replayedAnimationNodes;
                }
            }
        }

        const Skeleton &restoredSkeleton = GetSkeleton();
        const auto &restoredClips = GetAnimationClips();
        PE_INFO("[Scene] RestoreSnapshot end: geometryMatch=%d nodes=%u meshes=%zu skinnedNodes=%d bones=%zu clips=%zu replayed=%d",
                geometryMatch ? 1 : 0,
                GetNodeCount(),
                m_meshes.size(),
                CountSkinnedAnimationNodes(*this),
                restoredSkeleton.bones.size(),
                restoredClips.size(),
                replayedAnimationNodes);
    }
} // namespace pe
