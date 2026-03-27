#include "MaterialAsset.h"
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
    namespace MaterialAsset
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
            d.AddMember("texture_mask", mesh.textureMask, alloc);

            // Encode material factors — preserve the Transmission infinity sentinel
            mat4 f0 = mesh.materialFactors[0];
            mat4 f1 = mesh.materialFactors[1];
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
                Image *img = mesh.images[i].get();
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
                mesh.materialFactors[0] = ReadMat4(fa[0]);
                mesh.materialFactors[1] = ReadMat4(fa[1]);
                // Restore infinity from sentinel
                if (mesh.renderType == RenderType::Transmission && mesh.materialFactors[1][0][1] <= 0.0f)
                    mesh.materialFactors[1][0][1] = std::numeric_limits<float>::infinity();
            }

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
                            auto shared = std::shared_ptr<Image>(rawImg, [](Image *p) { Image::Destroy(p); });
                            ResourceManager::Get().Register<Image>(texStr, shared);
                            img = ResourceHandle<Image>(shared);
                        }
                    }
                    if (img)
                        mesh.images[i] = img;
                }

                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                queue->ReturnCommandBuffer(cmd);
            }

            // Apply texture mask after textures are loaded
            if (d.HasMember("texture_mask"))
                mesh.textureMask = d["texture_mask"].GetUint();

            return true;
        }
    }
} // namespace pe
