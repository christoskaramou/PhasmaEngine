#include "Scene/MaterialAsset.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/SceneNode.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

namespace pe
{
    namespace MaterialIO
    {
        static constexpr float kInfiniteAttenuationSentinel = -1.0f;
        static constexpr const char *kTexSlotNames[] = {
            "base_color", "metallic_roughness", "normal", "occlusion", "emissive"};

        void Save(const Mesh &mesh, const std::filesystem::path &path)
        {
            rapidjson::Document d;
            d.SetObject();
            auto &alloc = d.GetAllocator();

            d.AddMember("render_type", static_cast<int>(mesh.renderType), alloc);
            d.AddMember("texture_mask", mesh.material ? mesh.material->textureMask : 0u, alloc);

            // Encode material factors — preserve the Transmission infinity sentinel
            mat4 factors[2] = {mat4(1.f), mat4(1.f)};
            if (mesh.material)
                mesh.material->PackLegacyFactors(factors);
            mat4 f0 = factors[0];
            mat4 f1 = factors[1];
            if (mesh.renderType == RenderType::Transmission && std::isinf(f1[0][1]))
                f1[0][1] = kInfiniteAttenuationSentinel;

            auto WriteMat4 = [&alloc](const mat4 &m)
            {
                rapidjson::Value arr(rapidjson::kArrayType);
                const float *p = value_ptr(m);
                for (int i = 0; i < 16; i++)
                    arr.PushBack(p[i], alloc);
                return arr;
            };

            rapidjson::Value factorsArr(rapidjson::kArrayType);
            factorsArr.PushBack(WriteMat4(f0).Move(), alloc);
            factorsArr.PushBack(WriteMat4(f1).Move(), alloc);
            d.AddMember("material_factors", factorsArr.Move(), alloc);

            // Texture paths relative to the .mat file location
            rapidjson::Value texObj(rapidjson::kObjectType);
            const auto &defaults = ModelAsset::GetDefaultResources();
            for (int i = 0; i < 5; i++)
            {
                std::string texPath;
                Image *img = mesh.material ? mesh.material->textures[i].get() : nullptr;
                if (img && !img->GetName().empty() &&
                    img != defaults.black && img != defaults.white && img != defaults.normal)
                {
                    auto rel = std::filesystem::relative(std::filesystem::path(img->GetName()), path.parent_path());
                    texPath = rel.string();
                    std::replace(texPath.begin(), texPath.end(), '\\', '/');
                }
                texObj.AddMember(rapidjson::Value(kTexSlotNames[i], alloc).Move(),
                                 rapidjson::Value(texPath.c_str(), alloc).Move(), alloc);
            }
            d.AddMember("textures", texObj.Move(), alloc);

            std::filesystem::create_directories(path.parent_path());
            std::ofstream ofs(path);
            rapidjson::OStreamWrapper osw(ofs);
            rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
            d.Accept(writer);
        }

        bool Load(Mesh &mesh, const std::filesystem::path &path)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            rapidjson::IStreamWrapper isw(ifs);
            rapidjson::Document d;
            d.ParseStream(isw);
            if (d.HasParseError())
                return false;

            if (d.HasMember("render_type"))
                mesh.renderType = static_cast<RenderType>(d["render_type"].GetInt());

            if (mesh.material && d.HasMember("material_factors") && d["material_factors"].IsArray() && d["material_factors"].Size() >= 2)
            {
                const auto &fa = d["material_factors"];
                auto ReadMat4 = [](const rapidjson::Value &arr)
                {
                    mat4 m;
                    float *p = value_ptr(m);
                    for (int i = 0; i < 16; i++)
                        p[i] = arr[i].GetFloat();
                    return m;
                };
                mat4 factors[2];
                factors[0] = ReadMat4(fa[0]);
                factors[1] = ReadMat4(fa[1]);
                // Restore infinity from sentinel
                if (mesh.renderType == RenderType::Transmission && factors[1][0][1] <= 0.0f)
                    factors[1][0][1] = std::numeric_limits<float>::infinity();
                mesh.material->UnpackLegacyFactors(factors, mesh.material->textureMask);
            }

            // Clear texture slots so stale handles don't persist from previous material
            if (mesh.material)
                for (int i = 0; i < 5; i++)
                    mesh.material->textures[i] = ResourceHandle<Image>();

            if (d.HasMember("textures"))
            {
                const auto &texVal = d["textures"];

                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();

                for (int i = 0; i < 5; i++)
                {
                    if (!texVal.HasMember(kTexSlotNames[i]))
                        continue;
                    std::string relPath = texVal[kTexSlotNames[i]].GetString();
                    if (relPath.empty())
                        continue;

                    std::filesystem::path texPath(relPath);
                    if (texPath.is_relative())
                        texPath = (path.parent_path() / texPath).lexically_normal();
                    if (!std::filesystem::exists(texPath))
                        continue;

                    std::string texStr = texPath.string();
                    ResourceHandle<Image> img = ResourceManager::Get().Find<Image>(texStr);
                    if (!img)
                    {
                        Image *rawImg = Image::LoadRGBA8(cmd, texStr);
                        if (rawImg)
                        {
                            auto shared = std::shared_ptr<Image>(rawImg, [](Image *p)
                                                                 { Image::Destroy(p); });
                            ResourceManager::Get().Register<Image>(texStr, shared);
                            img = ResourceHandle<Image>(shared);
                        }
                    }
                    if (img && mesh.material)
                        mesh.material->textures[i] = img;
                }

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);
            }

            // Apply texture mask after textures are loaded
            if (d.HasMember("texture_mask") && mesh.material)
                mesh.material->textureMask = d["texture_mask"].GetUint();

            return true;
        }

        void SaveMaterial(const Material &material, const std::filesystem::path &path)
        {
            rapidjson::Document d;
            d.SetObject();
            auto &alloc = d.GetAllocator();

            d.AddMember("version", 2, alloc);
            d.AddMember("render_type", static_cast<int>(material.renderType), alloc);
            d.AddMember("double_sided", material.doubleSided, alloc);
            d.AddMember("texture_mask", material.textureMask, alloc);

            // Named parameters
            {
                rapidjson::Value bc(rapidjson::kArrayType);
                bc.PushBack(material.baseColorFactor.r, alloc);
                bc.PushBack(material.baseColorFactor.g, alloc);
                bc.PushBack(material.baseColorFactor.b, alloc);
                bc.PushBack(material.baseColorFactor.a, alloc);
                d.AddMember("base_color_factor", bc.Move(), alloc);
            }

            d.AddMember("metallic", material.metallic, alloc);
            d.AddMember("roughness", material.roughness, alloc);
            d.AddMember("alpha_cutoff", material.alphaCutoff, alloc);
            d.AddMember("occlusion_strength", material.occlusionStrength, alloc);
            d.AddMember("normal_scale", material.normalScale, alloc);

            {
                rapidjson::Value ef(rapidjson::kArrayType);
                ef.PushBack(material.emissiveFactor.r, alloc);
                ef.PushBack(material.emissiveFactor.g, alloc);
                ef.PushBack(material.emissiveFactor.b, alloc);
                d.AddMember("emissive_factor", ef.Move(), alloc);
            }

            d.AddMember("transmission_factor", material.transmissionFactor, alloc);
            d.AddMember("thickness_factor", material.thicknessFactor, alloc);

            // Encode attenuation distance: use sentinel for infinity
            float attDist = material.attenuationDistance;
            if (std::isinf(attDist))
                attDist = kInfiniteAttenuationSentinel;
            d.AddMember("attenuation_distance", attDist, alloc);
            d.AddMember("ior", material.ior, alloc);
            d.AddMember("night_emissive", material.nightEmissive, alloc);

            {
                rapidjson::Value ac(rapidjson::kArrayType);
                ac.PushBack(material.attenuationColor.r, alloc);
                ac.PushBack(material.attenuationColor.g, alloc);
                ac.PushBack(material.attenuationColor.b, alloc);
                d.AddMember("attenuation_color", ac.Move(), alloc);
            }

            // Also write legacy material_factors for backward compatibility
            {
                mat4 factors[2];
                material.PackLegacyFactors(factors);

                // Encode sentinel for legacy readers
                mat4 f1 = factors[1];
                if (material.renderType == RenderType::Transmission && std::isinf(f1[0][1]))
                    f1[0][1] = kInfiniteAttenuationSentinel;

                auto WriteMat4 = [&alloc](const mat4 &m)
                {
                    rapidjson::Value arr(rapidjson::kArrayType);
                    const float *p = value_ptr(m);
                    for (int i = 0; i < 16; i++)
                        arr.PushBack(p[i], alloc);
                    return arr;
                };

                rapidjson::Value factorsArr(rapidjson::kArrayType);
                factorsArr.PushBack(WriteMat4(factors[0]).Move(), alloc);
                factorsArr.PushBack(WriteMat4(f1).Move(), alloc);
                d.AddMember("material_factors", factorsArr.Move(), alloc);
            }

            // Texture paths
            rapidjson::Value texObj(rapidjson::kObjectType);
            const auto &defaults = ModelAsset::GetDefaultResources();
            for (int i = 0; i < 5; i++)
            {
                std::string texPath;
                Image *img = material.textures[i].get();
                if (img && !img->GetName().empty() &&
                    img != defaults.black && img != defaults.white && img != defaults.normal)
                {
                    auto rel = std::filesystem::relative(std::filesystem::path(img->GetName()), path.parent_path());
                    texPath = rel.string();
                    std::replace(texPath.begin(), texPath.end(), '\\', '/');
                }
                texObj.AddMember(rapidjson::Value(kTexSlotNames[i], alloc).Move(),
                                 rapidjson::Value(texPath.c_str(), alloc).Move(), alloc);
            }
            d.AddMember("textures", texObj.Move(), alloc);

            std::filesystem::create_directories(path.parent_path());
            std::ofstream ofs(path);
            rapidjson::OStreamWrapper osw(ofs);
            rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
            d.Accept(writer);
        }

        bool LoadMaterial(Material &material, const std::filesystem::path &path)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            rapidjson::IStreamWrapper isw(ifs);
            rapidjson::Document d;
            d.ParseStream(isw);
            if (d.HasParseError())
                return false;

            int version = 1;
            if (d.HasMember("version"))
                version = d["version"].GetInt();

            if (version >= 2)
            {
                // New format: read named parameters
                if (d.HasMember("render_type"))
                    material.renderType = static_cast<RenderType>(d["render_type"].GetInt());
                if (d.HasMember("double_sided"))
                    material.doubleSided = d["double_sided"].GetBool();

                if (d.HasMember("base_color_factor") && d["base_color_factor"].IsArray())
                {
                    const auto &a = d["base_color_factor"];
                    material.baseColorFactor = vec4(a[0].GetFloat(), a[1].GetFloat(), a[2].GetFloat(), a[3].GetFloat());
                }

                if (d.HasMember("metallic"))
                    material.metallic = d["metallic"].GetFloat();
                if (d.HasMember("roughness"))
                    material.roughness = d["roughness"].GetFloat();
                if (d.HasMember("alpha_cutoff"))
                    material.alphaCutoff = d["alpha_cutoff"].GetFloat();
                if (d.HasMember("occlusion_strength"))
                    material.occlusionStrength = d["occlusion_strength"].GetFloat();
                if (d.HasMember("normal_scale"))
                    material.normalScale = d["normal_scale"].GetFloat();

                if (d.HasMember("emissive_factor") && d["emissive_factor"].IsArray())
                {
                    const auto &a = d["emissive_factor"];
                    material.emissiveFactor = vec3(a[0].GetFloat(), a[1].GetFloat(), a[2].GetFloat());
                }

                if (d.HasMember("transmission_factor"))
                    material.transmissionFactor = d["transmission_factor"].GetFloat();
                if (d.HasMember("thickness_factor"))
                    material.thicknessFactor = d["thickness_factor"].GetFloat();

                if (d.HasMember("attenuation_distance"))
                {
                    float attDist = d["attenuation_distance"].GetFloat();
                    material.attenuationDistance = (attDist <= 0.f) ? std::numeric_limits<float>::infinity() : attDist;
                }

                if (d.HasMember("ior"))
                    material.ior = d["ior"].GetFloat();

                if (d.HasMember("night_emissive"))
                    material.nightEmissive = d["night_emissive"].GetFloat();

                if (d.HasMember("attenuation_color") && d["attenuation_color"].IsArray())
                {
                    const auto &a = d["attenuation_color"];
                    material.attenuationColor = vec3(a[0].GetFloat(), a[1].GetFloat(), a[2].GetFloat());
                }
            }
            else
            {
                // Legacy v1 format: read from material_factors
                if (d.HasMember("render_type"))
                    material.renderType = static_cast<RenderType>(d["render_type"].GetInt());

                if (d.HasMember("material_factors") && d["material_factors"].IsArray() && d["material_factors"].Size() >= 2)
                {
                    const auto &fa = d["material_factors"];
                    auto ReadMat4 = [](const rapidjson::Value &arr)
                    {
                        mat4 m;
                        float *p = value_ptr(m);
                        for (int i = 0; i < 16; i++)
                            p[i] = arr[i].GetFloat();
                        return m;
                    };

                    mat4 factors[2];
                    factors[0] = ReadMat4(fa[0]);
                    factors[1] = ReadMat4(fa[1]);

                    // Restore infinity from sentinel
                    if (material.renderType == RenderType::Transmission && factors[1][0][1] <= 0.0f)
                        factors[1][0][1] = std::numeric_limits<float>::infinity();

                    material.UnpackLegacyFactors(factors, 0);
                }
            }

            // Load textures (same for both versions)
            // Clear all texture slots first so slots absent from the file don't retain stale handles
            for (int i = 0; i < 5; i++)
                material.textures[i] = ResourceHandle<Image>();

            if (d.HasMember("textures"))
            {
                const auto &texVal = d["textures"];

                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();

                for (int i = 0; i < 5; i++)
                {
                    if (!texVal.HasMember(kTexSlotNames[i]))
                        continue;
                    std::string relPath = texVal[kTexSlotNames[i]].GetString();
                    if (relPath.empty())
                        continue;

                    std::filesystem::path texPath(relPath);
                    if (texPath.is_relative())
                        texPath = (path.parent_path() / texPath).lexically_normal();
                    if (!std::filesystem::exists(texPath))
                        continue;

                    std::string texStr = texPath.string();
                    ResourceHandle<Image> img = ResourceManager::Get().Find<Image>(texStr);
                    if (!img)
                    {
                        Image *rawImg = Image::LoadRGBA8(cmd, texStr);
                        if (rawImg)
                        {
                            auto shared = std::shared_ptr<Image>(rawImg, [](Image *p)
                                                                 { Image::Destroy(p); });
                            ResourceManager::Get().Register<Image>(texStr, shared);
                            img = ResourceHandle<Image>(shared);
                        }
                    }
                    if (img)
                        material.textures[i] = img;
                }

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);
            }

            // Apply texture mask after textures are loaded
            if (d.HasMember("texture_mask"))
                material.textureMask = d["texture_mask"].GetUint();

            material.filePath = path;
            return true;
        }
    } // namespace MaterialIO
} // namespace pe
