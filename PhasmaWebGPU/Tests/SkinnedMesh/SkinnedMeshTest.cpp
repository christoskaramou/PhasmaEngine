// 1:1 C++ port of webgpu-samples/sample/skinnedMesh.
// Inlines glbUtils.ts / gltf.ts / gridData.ts / gridUtils.ts / main.ts as a
// single translation unit with a minimal GLB 2.0 parser (sufficient for
// whale.glb: one mesh, one skin, 6 joints, procedural joint animation).

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"

#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr uint64_t kMat4Bytes = 64;

    enum class RenderMode : uint32_t
    {
        Normal = 0,
        Joints = 1,
        Weights = 2,
    };

    enum class SkinMode : uint32_t
    {
        On = 0,
        Off = 1,
    };

    // -------------------------------------------------------------------
    // gridData.ts
    // -------------------------------------------------------------------
    constexpr float kGridVertices[] = {
        // B0
        0.f,
        1.f,
        0.f,
        -1.f,
        // CONNECTOR
        2.f,
        1.f,
        2.f,
        -1.f,
        // B1
        4.f,
        1.f,
        4.f,
        -1.f,
        // CONNECTOR
        6.f,
        1.f,
        6.f,
        -1.f,
        // B2
        8.f,
        1.f,
        8.f,
        -1.f,
        // CONNECTOR
        10.f,
        1.f,
        10.f,
        -1.f,
        // B3
        12.f,
        1.f,
        12.f,
        -1.f,
    };

    constexpr uint32_t kGridJoints[] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        1,
        2,
        0,
        0,
        1,
        2,
        0,
        0,
        2,
        0,
        0,
        0,
        2,
        0,
        0,
        0,
        1,
        2,
        3,
        0,
        1,
        2,
        3,
        0,
        2,
        3,
        0,
        0,
        2,
        3,
        0,
        0,
    };

    constexpr float kGridWeights[] = {
        1.f,
        0.f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        .5f,
        .5f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
        1.f,
        0.f,
        0.f,
        0.f,
    };

    constexpr uint16_t kGridIndices[] = {
        0,
        1,
        0,
        2,
        1,
        3,
        2,
        3,
        2,
        4,
        3,
        5,
        4,
        5,
        4,
        6,
        5,
        7,
        6,
        7,
        6,
        8,
        7,
        9,
        8,
        9,
        8,
        10,
        9,
        11,
        10,
        11,
        10,
        12,
        11,
        13,
        12,
        13,
    };

    // -------------------------------------------------------------------
    // Minimal GLB 2.0 parser. Sufficient for whale.glb's feature set:
    //   - 1 mesh with POSITION(f32x3) NORMAL(f32x3) TEXCOORD_0 JOINTS_0(u8x4)
    //     WEIGHTS_0(f32x4), indices u16.
    //   - 1 skin with f32 mat4 inverseBindMatrices, 6 joints.
    //   - Multiple animations in the file (unused by upstream: joints are
    //     animated procedurally in animWhaleSkin()).
    // -------------------------------------------------------------------

    struct GlbBufferView
    {
        uint32_t byteOffset = 0;
        uint32_t byteLength = 0;
        uint32_t byteStride = 0;
    };

    struct GlbAccessor
    {
        uint32_t bufferView = 0;
        uint32_t byteOffset = 0;
        uint32_t componentType = 0; // 5120..5126
        uint32_t count = 0;
        std::string type; // "SCALAR" / "VEC2" / "VEC3" / "VEC4" / "MAT4"
    };

    struct BaseTransform
    {
        std::array<float, 3> position = {0.f, 0.f, 0.f};
        std::array<float, 4> rotation = {0.f, 0.f, 0.f, 1.f};
        std::array<float, 3> scale = {1.f, 1.f, 1.f};
    };

    struct GlbNode
    {
        std::string name;
        BaseTransform transform;
        std::vector<uint32_t> children;
        int mesh = -1;
        int skin = -1;
        int parent = -1;
        mat4 localMatrix{1.f};
        mat4 worldMatrix{1.f};
    };

    struct GlbScene
    {
        std::vector<uint32_t> rootNodes;
    };

    struct GlbSkin
    {
        std::vector<uint32_t> joints;
        int inverseBindMatricesAccessor = -1;
        std::vector<mat4> inverseBindMatrices; // column-major, copied from BIN
    };

    struct GlbPrimitive
    {
        int positionAccessor = -1;
        int normalAccessor = -1;
        int joints0Accessor = -1;
        int weights0Accessor = -1;
        int indicesAccessor = -1;
    };

    struct GlbMesh
    {
        std::string name;
        std::vector<GlbPrimitive> primitives;
    };

    struct GlbScenePack
    {
        std::vector<GlbBufferView> bufferViews;
        std::vector<GlbAccessor> accessors;
        std::vector<GlbNode> nodes;
        std::vector<GlbScene> scenes;
        std::vector<GlbSkin> skins;
        std::vector<GlbMesh> meshes;
        std::vector<uint8_t> binary;
    };

    // Build a column-major 4x4 matrix from TRS using glm (column-major by default).
    mat4 ComposeTRS(const BaseTransform &t)
    {
        // glTF stores quaternion as [x,y,z,w]; glm::quat ctor takes (w,x,y,z).
        glm::quat q(t.rotation[3], t.rotation[0], t.rotation[1], t.rotation[2]);
        mat4 r = glm::mat4_cast(q);
        mat4 s = glm::scale(mat4(1.f), vec3(t.scale[0], t.scale[1], t.scale[2]));
        mat4 tr = glm::translate(mat4(1.f), vec3(t.position[0], t.position[1], t.position[2]));
        return tr * r * s;
    }

    bool ReadGlbJsonMember(const rapidjson::Value &obj, const char *key, uint32_t &out)
    {
        if (!obj.HasMember(key))
            return false;
        out = obj[key].GetUint();
        return true;
    }

    bool LoadGlb(const std::string &path, GlbScenePack &pack)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            fprintf(stderr, "[GLB] cannot open %s\n", path.c_str());
            return false;
        }

        uint32_t header[3] = {};
        if (std::fread(header, sizeof(uint32_t), 3, file) != 3)
        {
            std::fclose(file);
            return false;
        }
        if (header[0] != 0x46546C67u)
        {
            fprintf(stderr, "[GLB] bad magic %08X\n", header[0]);
            std::fclose(file);
            return false;
        }
        if (header[1] != 2u)
        {
            fprintf(stderr, "[GLB] unsupported version %u\n", header[1]);
            std::fclose(file);
            return false;
        }

        uint32_t jsonChunk[2] = {};
        if (std::fread(jsonChunk, sizeof(uint32_t), 2, file) != 2)
        {
            std::fclose(file);
            return false;
        }
        if (jsonChunk[1] != 0x4E4F534Au) // "JSON"
        {
            fprintf(stderr, "[GLB] first chunk is not JSON (type=%08X)\n", jsonChunk[1]);
            std::fclose(file);
            return false;
        }
        std::string jsonText(jsonChunk[0], '\0');
        if (std::fread(jsonText.data(), 1, jsonChunk[0], file) != jsonChunk[0])
        {
            std::fclose(file);
            return false;
        }

        uint32_t binChunk[2] = {};
        if (std::fread(binChunk, sizeof(uint32_t), 2, file) != 2)
        {
            std::fclose(file);
            return false;
        }
        if (binChunk[1] != 0x004E4942u) // "BIN\0"
        {
            fprintf(stderr, "[GLB] second chunk is not BIN (type=%08X)\n", binChunk[1]);
            std::fclose(file);
            return false;
        }
        pack.binary.resize(binChunk[0]);
        if (std::fread(pack.binary.data(), 1, binChunk[0], file) != binChunk[0])
        {
            std::fclose(file);
            return false;
        }
        std::fclose(file);

        rapidjson::Document doc;
        doc.Parse(jsonText.c_str(), jsonText.size());
        if (doc.HasParseError() || !doc.IsObject())
        {
            fprintf(stderr, "[GLB] JSON parse error\n");
            return false;
        }

        // bufferViews
        if (doc.HasMember("bufferViews"))
        {
            const auto &arr = doc["bufferViews"];
            pack.bufferViews.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &bv = arr[i];
                GlbBufferView &out = pack.bufferViews[i];
                ReadGlbJsonMember(bv, "byteOffset", out.byteOffset);
                ReadGlbJsonMember(bv, "byteLength", out.byteLength);
                ReadGlbJsonMember(bv, "byteStride", out.byteStride);
            }
        }

        // accessors
        if (doc.HasMember("accessors"))
        {
            const auto &arr = doc["accessors"];
            pack.accessors.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &ac = arr[i];
                GlbAccessor &out = pack.accessors[i];
                ReadGlbJsonMember(ac, "bufferView", out.bufferView);
                ReadGlbJsonMember(ac, "byteOffset", out.byteOffset);
                ReadGlbJsonMember(ac, "componentType", out.componentType);
                ReadGlbJsonMember(ac, "count", out.count);
                if (ac.HasMember("type"))
                    out.type = ac["type"].GetString();
            }
        }

        // meshes
        if (doc.HasMember("meshes"))
        {
            const auto &arr = doc["meshes"];
            pack.meshes.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &m = arr[i];
                GlbMesh &out = pack.meshes[i];
                if (m.HasMember("name"))
                    out.name = m["name"].GetString();
                if (m.HasMember("primitives"))
                {
                    const auto &prims = m["primitives"];
                    out.primitives.resize(prims.Size());
                    for (rapidjson::SizeType p = 0; p < prims.Size(); ++p)
                    {
                        GlbPrimitive &gp = out.primitives[p];
                        const auto &pp = prims[p];
                        if (pp.HasMember("attributes"))
                        {
                            const auto &attrs = pp["attributes"];
                            for (auto it = attrs.MemberBegin(); it != attrs.MemberEnd(); ++it)
                            {
                                const char *key = it->name.GetString();
                                int idx = static_cast<int>(it->value.GetUint());
                                if (std::strcmp(key, "POSITION") == 0)
                                    gp.positionAccessor = idx;
                                else if (std::strcmp(key, "NORMAL") == 0)
                                    gp.normalAccessor = idx;
                                else if (std::strcmp(key, "JOINTS_0") == 0)
                                    gp.joints0Accessor = idx;
                                else if (std::strcmp(key, "WEIGHTS_0") == 0)
                                    gp.weights0Accessor = idx;
                            }
                        }
                        if (pp.HasMember("indices"))
                            gp.indicesAccessor = static_cast<int>(pp["indices"].GetUint());
                    }
                }
            }
        }

        // skins
        if (doc.HasMember("skins"))
        {
            const auto &arr = doc["skins"];
            pack.skins.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &s = arr[i];
                GlbSkin &out = pack.skins[i];
                if (s.HasMember("inverseBindMatrices"))
                    out.inverseBindMatricesAccessor =
                        static_cast<int>(s["inverseBindMatrices"].GetUint());
                if (s.HasMember("joints"))
                {
                    const auto &j = s["joints"];
                    out.joints.resize(j.Size());
                    for (rapidjson::SizeType k = 0; k < j.Size(); ++k)
                        out.joints[k] = j[k].GetUint();
                }

                // Decode the inverseBindMatrices accessor into a column-major mat4 array.
                if (out.inverseBindMatricesAccessor >= 0)
                {
                    const GlbAccessor &a = pack.accessors[out.inverseBindMatricesAccessor];
                    if (a.componentType != 5126 || a.type != "MAT4")
                    {
                        fprintf(stderr,
                                "[GLB] inverseBindMatrices accessor is not MAT4 float\n");
                        return false;
                    }
                    const GlbBufferView &bv = pack.bufferViews[a.bufferView];
                    const uint8_t *base = pack.binary.data() + bv.byteOffset + a.byteOffset;
                    out.inverseBindMatrices.resize(a.count);
                    // glTF stores matrices column-major, glm mat4 is also column-major.
                    std::memcpy(out.inverseBindMatrices.data(),
                                base,
                                static_cast<size_t>(a.count) * sizeof(mat4));
                }
            }
        }

        // nodes
        if (doc.HasMember("nodes"))
        {
            const auto &arr = doc["nodes"];
            pack.nodes.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &n = arr[i];
                GlbNode &out = pack.nodes[i];
                if (n.HasMember("name"))
                    out.name = n["name"].GetString();
                if (n.HasMember("mesh"))
                    out.mesh = static_cast<int>(n["mesh"].GetUint());
                if (n.HasMember("skin"))
                    out.skin = static_cast<int>(n["skin"].GetUint());
                if (n.HasMember("translation"))
                {
                    const auto &t = n["translation"];
                    for (rapidjson::SizeType k = 0; k < 3 && k < t.Size(); ++k)
                        out.transform.position[k] = t[k].GetFloat();
                }
                if (n.HasMember("rotation"))
                {
                    const auto &r = n["rotation"];
                    for (rapidjson::SizeType k = 0; k < 4 && k < r.Size(); ++k)
                        out.transform.rotation[k] = r[k].GetFloat();
                }
                if (n.HasMember("scale"))
                {
                    const auto &s = n["scale"];
                    for (rapidjson::SizeType k = 0; k < 3 && k < s.Size(); ++k)
                        out.transform.scale[k] = s[k].GetFloat();
                }
                if (n.HasMember("children"))
                {
                    const auto &c = n["children"];
                    out.children.resize(c.Size());
                    for (rapidjson::SizeType k = 0; k < c.Size(); ++k)
                        out.children[k] = c[k].GetUint();
                }
            }
            // Build parent links.
            for (size_t i = 0; i < pack.nodes.size(); ++i)
            {
                for (uint32_t child : pack.nodes[i].children)
                    pack.nodes[child].parent = static_cast<int>(i);
            }
        }

        // scenes
        if (doc.HasMember("scenes"))
        {
            const auto &arr = doc["scenes"];
            pack.scenes.resize(arr.Size());
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                const auto &s = arr[i];
                if (s.HasMember("nodes"))
                {
                    const auto &nn = s["nodes"];
                    pack.scenes[i].rootNodes.resize(nn.Size());
                    for (rapidjson::SizeType k = 0; k < nn.Size(); ++k)
                        pack.scenes[i].rootNodes[k] = nn[k].GetUint();
                }
            }
        }

        return true;
    }

    // Copy bytes out of a glTF accessor into a contiguous typed buffer sized
    // accessor.count * elementSize. Works for non-interleaved bufferViews
    // (byteStride == 0 or == elementSize) which is the case for whale.glb.
    bool ExtractAccessorBytes(const GlbScenePack &pack,
                              const GlbAccessor &acc,
                              uint32_t elementSize,
                              std::vector<uint8_t> &out)
    {
        const GlbBufferView &bv = pack.bufferViews[acc.bufferView];
        const uint8_t *base = pack.binary.data() + bv.byteOffset + acc.byteOffset;
        const uint32_t stride = bv.byteStride == 0 ? elementSize : bv.byteStride;
        out.resize(static_cast<size_t>(acc.count) * elementSize);
        if (stride == elementSize)
        {
            std::memcpy(out.data(), base, out.size());
        }
        else
        {
            for (uint32_t i = 0; i < acc.count; ++i)
                std::memcpy(out.data() + i * elementSize, base + i * stride, elementSize);
        }
        return true;
    }

    // -------------------------------------------------------------------
    // main.ts — application body.
    // -------------------------------------------------------------------

    struct Settings
    {
        float cameraX = 0.f;
        float cameraY = -5.1f;
        float cameraZ = -14.6f;
        float objectScale = 1.f;
        float angle = 0.2f;
        float speed = 50.f;
        bool renderWhale = true; // "Whale" vs "Skinned Grid"
        RenderMode renderMode = RenderMode::Normal;
        SkinMode skinMode = SkinMode::On;
    };

    class SkinnedMeshSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            const std::string glbPath = pwgpu::test::GetAssetPath(
                ctx.exeDir, "gltf/whale.glb");
            if (!LoadGlb(glbPath, m_pack))
            {
                fprintf(stderr, "[SkinnedMesh] failed to load %s\n", glbPath.c_str());
                return false;
            }
            if (m_pack.meshes.empty() || m_pack.skins.empty())
            {
                fprintf(stderr, "[SkinnedMesh] whale.glb missing mesh or skin\n");
                return false;
            }
            fprintf(stdout,
                    "[SkinnedMesh] nodes=%zu meshes=%zu skins=%zu joints=%zu\n",
                    m_pack.nodes.size(),
                    m_pack.meshes.size(),
                    m_pack.skins.size(),
                    m_pack.skins[0].joints.size());

            if (!LoadGltfShaders(ctx))
                return false;
            if (!LoadGridShaders(ctx))
                return false;
            if (!CreateSharedBuffers(ctx))
                return false;
            if (!CreateGltfResources(ctx))
                return false;
            if (!CreateGridResources(ctx))
                return false;
            WriteGeneralUniforms(ctx);
            InitOrigMatrices();
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"skinned_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_Depth24Plus;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &viewDesc);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect = ctx.height > 0
                                     ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                                     : 1.f;
            const float time = static_cast<float>(ctx.timing.elapsedSeconds);

            // main.ts: t = (Date.now() / 20000) * settings.speed
            const float t = (time / 20.f) * m_settings.speed;
            const float angle = std::sin(t) * m_settings.angle;

            // projection
            mat4 projection;
            if (m_settings.renderWhale)
            {
                projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 0.1f, 100.f);
            }
            else
            {
                projection = glm::orthoRH_ZO(-20.f, 20.f, -10.f, 10.f, -100.f, 100.f);
            }
            projection[1][1] *= -1.f;

            // view
            mat4 view(1.f);
            if (m_settings.renderWhale)
            {
                view = glm::translate(view,
                                      vec3(m_settings.cameraX, m_settings.cameraY, m_settings.cameraZ));
            }
            else
            {
                view = glm::translate(
                    view,
                    vec3(m_settings.cameraX * m_settings.objectScale,
                         m_settings.cameraY * m_settings.objectScale,
                         m_settings.cameraZ));
            }

            // model
            mat4 model = glm::scale(mat4(1.f),
                                    vec3(m_settings.objectScale,
                                         m_settings.objectScale,
                                         m_settings.objectScale));
            if (m_settings.renderWhale)
            {
                model = glm::rotate(model, time * 0.5f, vec3(0.f, 1.f, 0.f));
            }

            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 0, glm::value_ptr(projection), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 64, glm::value_ptr(view), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_cameraBuffer, 128, glm::value_ptr(model), 64);

            // Grid bone transforms (animSkinnedGrid): clock-driven rotateZ chain.
            {
                mat4 m(1.f);
                mat4 r0 = glm::rotate(m, angle, vec3(0.f, 0.f, 1.f));
                m_gridBoneTransforms[0] = r0;
                mat4 tx0 = glm::translate(r0, vec3(4.f, 0.f, 0.f));

                mat4 r1 = glm::rotate(tx0, angle, vec3(0.f, 0.f, 1.f));
                m_gridBoneTransforms[1] = r1;
                mat4 tx1 = glm::translate(r1, vec3(4.f, 0.f, 0.f));

                mat4 r2 = glm::rotate(tx1, angle, vec3(0.f, 0.f, 1.f));
                m_gridBoneTransforms[2] = r2;
                m_gridBoneTransforms[3] = mat4(1.f);
                m_gridBoneTransforms[4] = mat4(1.f);
            }
            for (uint32_t i = 0; i < kGridBones; ++i)
            {
                wgpuQueueWriteBuffer(ctx.queue,
                                     m_gridJointBuffer,
                                     static_cast<uint64_t>(i) * kMat4Bytes,
                                     glm::value_ptr(m_gridBoneTransforms[i]),
                                     kMat4Bytes);
            }

            // animWhaleSkin — rotate each whale joint based on its index.
            AnimateWhaleSkin(angle);
            UpdateNodeWorldMatrices(ctx);
            UploadSkinJointMatrices(ctx);
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (m_settings.renderWhale)
                return ExecuteWhalePass(frame);
            return ExecuteGridPass(frame);
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_whalePipeline)
                wgpuRenderPipelineRelease(m_whalePipeline);
            if (m_whalePipelineLayout)
                wgpuPipelineLayoutRelease(m_whalePipelineLayout);
            if (m_gridPipeline)
                wgpuRenderPipelineRelease(m_gridPipeline);
            if (m_gridPipelineLayout)
                wgpuPipelineLayoutRelease(m_gridPipelineLayout);

            if (m_whaleSkinBindGroup)
                wgpuBindGroupRelease(m_whaleSkinBindGroup);
            if (m_whaleSkinBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_whaleSkinBindGroupLayout);
            if (m_whaleNodeBindGroup)
                wgpuBindGroupRelease(m_whaleNodeBindGroup);
            if (m_whaleNodeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_whaleNodeBindGroupLayout);
            if (m_gridSkinBindGroup)
                wgpuBindGroupRelease(m_gridSkinBindGroup);
            if (m_gridSkinBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_gridSkinBindGroupLayout);
            if (m_generalBindGroup)
                wgpuBindGroupRelease(m_generalBindGroup);
            if (m_generalBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_generalBindGroupLayout);
            if (m_cameraBindGroup)
                wgpuBindGroupRelease(m_cameraBindGroup);
            if (m_cameraBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_cameraBindGroupLayout);

            if (m_whalePositionBuffer)
                wgpuBufferRelease(m_whalePositionBuffer);
            if (m_whaleNormalBuffer)
                wgpuBufferRelease(m_whaleNormalBuffer);
            if (m_whaleJointsBuffer)
                wgpuBufferRelease(m_whaleJointsBuffer);
            if (m_whaleWeightsBuffer)
                wgpuBufferRelease(m_whaleWeightsBuffer);
            if (m_whaleIndexBuffer)
                wgpuBufferRelease(m_whaleIndexBuffer);
            if (m_whaleNodeBuffer)
                wgpuBufferRelease(m_whaleNodeBuffer);
            if (m_whaleJointMatrixBuffer)
                wgpuBufferRelease(m_whaleJointMatrixBuffer);
            if (m_whaleInverseBindBuffer)
                wgpuBufferRelease(m_whaleInverseBindBuffer);

            if (m_gridPositionBuffer)
                wgpuBufferRelease(m_gridPositionBuffer);
            if (m_gridJointsBuffer)
                wgpuBufferRelease(m_gridJointsBuffer);
            if (m_gridWeightsBuffer)
                wgpuBufferRelease(m_gridWeightsBuffer);
            if (m_gridIndexBuffer)
                wgpuBufferRelease(m_gridIndexBuffer);
            if (m_gridJointBuffer)
                wgpuBufferRelease(m_gridJointBuffer);
            if (m_gridInverseBindBuffer)
                wgpuBufferRelease(m_gridInverseBindBuffer);

            if (m_generalBuffer)
                wgpuBufferRelease(m_generalBuffer);
            if (m_cameraBuffer)
                wgpuBufferRelease(m_cameraBuffer);

            if (m_whaleVertexShader)
                wgpuShaderModuleRelease(m_whaleVertexShader);
            if (m_whalePixelShader)
                wgpuShaderModuleRelease(m_whalePixelShader);
            if (m_gridVertexShader)
                wgpuShaderModuleRelease(m_gridVertexShader);
            if (m_gridPixelShader)
                wgpuShaderModuleRelease(m_gridPixelShader);
        }

    private:
        static constexpr uint32_t kGridBones = 5;

        bool LoadGltfShaders(pwgpu::test::SampleContext &ctx)
        {
            m_whaleVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "gltf.vert.hlsl").c_str(), "gltf_vs");
            m_whalePixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "gltf.pixel.hlsl").c_str(), "gltf_ps");
            return m_whaleVertexShader && m_whalePixelShader;
        }

        bool LoadGridShaders(pwgpu::test::SampleContext &ctx)
        {
            m_gridVertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "grid.vert.hlsl").c_str(), "grid_vs");
            m_gridPixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "grid.pixel.hlsl").c_str(), "grid_ps");
            return m_gridVertexShader && m_gridPixelShader;
        }

        bool CreateSharedBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_cameraBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kMat4Bytes * 3,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "camera_ub");
            m_generalBuffer = pwgpu::test::CreateBuffer(
                ctx.device, 16,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "general_ub");
            return m_cameraBuffer && m_generalBuffer;
        }

        void WriteGeneralUniforms(pwgpu::test::SampleContext &ctx)
        {
            uint32_t data[4] = {
                static_cast<uint32_t>(m_settings.renderMode),
                static_cast<uint32_t>(m_settings.skinMode),
                0u,
                0u,
            };
            wgpuQueueWriteBuffer(ctx.queue, m_generalBuffer, 0, data, sizeof(data));
        }

        // -------------------------------------------------------------------
        // Whale resource creation — matches upstream buildRenderPipeline: four
        // separate vertex buffers for POSITION/NORMAL/JOINTS_0/WEIGHTS_0.
        // -------------------------------------------------------------------
        bool CreateGltfResources(pwgpu::test::SampleContext &ctx)
        {
            const GlbPrimitive &prim = m_pack.meshes[0].primitives[0];

            if (prim.positionAccessor < 0 || prim.normalAccessor < 0 ||
                prim.joints0Accessor < 0 || prim.weights0Accessor < 0 ||
                prim.indicesAccessor < 0)
            {
                fprintf(stderr, "[SkinnedMesh] primitive missing required accessor\n");
                return false;
            }

            const GlbAccessor &aPos = m_pack.accessors[prim.positionAccessor];
            const GlbAccessor &aNrm = m_pack.accessors[prim.normalAccessor];
            const GlbAccessor &aJnt = m_pack.accessors[prim.joints0Accessor];
            const GlbAccessor &aWgt = m_pack.accessors[prim.weights0Accessor];
            const GlbAccessor &aIdx = m_pack.accessors[prim.indicesAccessor];

            std::vector<uint8_t> posBytes, nrmBytes, jntBytes, wgtBytes, idxBytes;
            ExtractAccessorBytes(m_pack, aPos, 12u, posBytes);
            ExtractAccessorBytes(m_pack, aNrm, 12u, nrmBytes);
            // JOINTS_0 componentType 5121 (UNSIGNED_BYTE) VEC4 → 4 bytes per vertex.
            const uint32_t jointElem = (aJnt.componentType == 5123) ? 8u : 4u;
            ExtractAccessorBytes(m_pack, aJnt, jointElem, jntBytes);
            ExtractAccessorBytes(m_pack, aWgt, 16u, wgtBytes);
            // INDICES componentType 5123 (UNSIGNED_SHORT) → 2 bytes.
            ExtractAccessorBytes(m_pack, aIdx, 2u, idxBytes);

            m_whaleIndexCount = aIdx.count;
            m_whaleJointIsU16 = (aJnt.componentType == 5123);

            m_whalePositionBuffer = pwgpu::test::CreateBuffer(
                ctx.device, posBytes.size(),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "whale_pos");
            m_whaleNormalBuffer = pwgpu::test::CreateBuffer(
                ctx.device, nrmBytes.size(),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "whale_nrm");
            m_whaleJointsBuffer = pwgpu::test::CreateBuffer(
                ctx.device, jntBytes.size(),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "whale_jnt");
            m_whaleWeightsBuffer = pwgpu::test::CreateBuffer(
                ctx.device, wgtBytes.size(),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "whale_wgt");

            // Index buffer must be 4-byte aligned for WriteBuffer.
            const uint64_t idxSize = (static_cast<uint64_t>(idxBytes.size()) + 3u) & ~uint64_t(3);
            m_whaleIndexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, idxSize,
                WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, "whale_idx");

            if (!m_whalePositionBuffer || !m_whaleNormalBuffer || !m_whaleJointsBuffer ||
                !m_whaleWeightsBuffer || !m_whaleIndexBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue, m_whalePositionBuffer, 0, posBytes.data(), posBytes.size());
            wgpuQueueWriteBuffer(ctx.queue, m_whaleNormalBuffer, 0, nrmBytes.data(), nrmBytes.size());
            wgpuQueueWriteBuffer(ctx.queue, m_whaleJointsBuffer, 0, jntBytes.data(), jntBytes.size());
            wgpuQueueWriteBuffer(ctx.queue, m_whaleWeightsBuffer, 0, wgtBytes.data(), wgtBytes.size());
            std::vector<uint8_t> paddedIdx(static_cast<size_t>(idxSize), 0);
            std::memcpy(paddedIdx.data(), idxBytes.data(), idxBytes.size());
            wgpuQueueWriteBuffer(ctx.queue, m_whaleIndexBuffer, 0, paddedIdx.data(), paddedIdx.size());

            // Node uniform buffer (per drawable-node world matrix).
            m_whaleNodeBuffer = pwgpu::test::CreateBuffer(
                ctx.device, kMat4Bytes,
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "whale_node_ub");

            // Skin storage buffers — one entry per joint.
            const uint64_t skinBufferSize = kMat4Bytes * m_pack.skins[0].joints.size();
            m_whaleJointMatrixBuffer = pwgpu::test::CreateBuffer(
                ctx.device, skinBufferSize,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "whale_skin_jnt");
            m_whaleInverseBindBuffer = pwgpu::test::CreateBuffer(
                ctx.device, skinBufferSize,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "whale_skin_ibm");
            if (!m_whaleNodeBuffer || !m_whaleJointMatrixBuffer || !m_whaleInverseBindBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_whaleInverseBindBuffer, 0,
                                 m_pack.skins[0].inverseBindMatrices.data(), skinBufferSize);

            if (!CreateGltfBindGroupLayouts(ctx))
                return false;
            if (!CreateGltfBindGroups(ctx))
                return false;
            if (!CreateGltfPipeline(ctx))
                return false;
            return true;
        }

        bool CreateGltfBindGroupLayouts(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry cam{};
            cam.binding = 0;
            cam.visibility = WGPUShaderStage_Vertex;
            cam.buffer.type = WGPUBufferBindingType_Uniform;
            cam.buffer.minBindingSize = kMat4Bytes * 3;
            WGPUBindGroupLayoutDescriptor camDesc{};
            camDesc.label = {"cam_bgl", WGPU_STRLEN};
            camDesc.entryCount = 1;
            camDesc.entries = &cam;
            m_cameraBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &camDesc);

            WGPUBindGroupLayoutEntry gen{};
            gen.binding = 0;
            gen.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            gen.buffer.type = WGPUBufferBindingType_Uniform;
            gen.buffer.minBindingSize = 16;
            WGPUBindGroupLayoutDescriptor genDesc{};
            genDesc.label = {"gen_bgl", WGPU_STRLEN};
            genDesc.entryCount = 1;
            genDesc.entries = &gen;
            m_generalBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &genDesc);

            WGPUBindGroupLayoutEntry node{};
            node.binding = 0;
            node.visibility = WGPUShaderStage_Vertex;
            node.buffer.type = WGPUBufferBindingType_Uniform;
            node.buffer.minBindingSize = kMat4Bytes;
            WGPUBindGroupLayoutDescriptor nodeDesc{};
            nodeDesc.label = {"whale_node_bgl", WGPU_STRLEN};
            nodeDesc.entryCount = 1;
            nodeDesc.entries = &node;
            m_whaleNodeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &nodeDesc);

            WGPUBindGroupLayoutEntry skin[2] = {};
            skin[0].binding = 0;
            skin[0].visibility = WGPUShaderStage_Vertex;
            skin[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            skin[1].binding = 1;
            skin[1].visibility = WGPUShaderStage_Vertex;
            skin[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            WGPUBindGroupLayoutDescriptor skinDesc{};
            skinDesc.label = {"whale_skin_bgl", WGPU_STRLEN};
            skinDesc.entryCount = 2;
            skinDesc.entries = skin;
            m_whaleSkinBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &skinDesc);

            return m_cameraBindGroupLayout && m_generalBindGroupLayout &&
                   m_whaleNodeBindGroupLayout && m_whaleSkinBindGroupLayout;
        }

        bool CreateGltfBindGroups(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupEntry e{};
            e.binding = 0;
            e.buffer = m_cameraBuffer;
            e.size = kMat4Bytes * 3;
            WGPUBindGroupDescriptor d{};
            d.label = {"cam_bg", WGPU_STRLEN};
            d.layout = m_cameraBindGroupLayout;
            d.entryCount = 1;
            d.entries = &e;
            m_cameraBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &d);

            WGPUBindGroupEntry g{};
            g.binding = 0;
            g.buffer = m_generalBuffer;
            g.size = 16;
            WGPUBindGroupDescriptor gd{};
            gd.label = {"gen_bg", WGPU_STRLEN};
            gd.layout = m_generalBindGroupLayout;
            gd.entryCount = 1;
            gd.entries = &g;
            m_generalBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &gd);

            WGPUBindGroupEntry n{};
            n.binding = 0;
            n.buffer = m_whaleNodeBuffer;
            n.size = kMat4Bytes;
            WGPUBindGroupDescriptor nd{};
            nd.label = {"whale_node_bg", WGPU_STRLEN};
            nd.layout = m_whaleNodeBindGroupLayout;
            nd.entryCount = 1;
            nd.entries = &n;
            m_whaleNodeBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &nd);

            const uint64_t skinBufferSize = kMat4Bytes * m_pack.skins[0].joints.size();
            WGPUBindGroupEntry s[2] = {};
            s[0].binding = 0;
            s[0].buffer = m_whaleJointMatrixBuffer;
            s[0].size = skinBufferSize;
            s[1].binding = 1;
            s[1].buffer = m_whaleInverseBindBuffer;
            s[1].size = skinBufferSize;
            WGPUBindGroupDescriptor sd{};
            sd.label = {"whale_skin_bg", WGPU_STRLEN};
            sd.layout = m_whaleSkinBindGroupLayout;
            sd.entryCount = 2;
            sd.entries = s;
            m_whaleSkinBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &sd);

            return m_cameraBindGroup && m_generalBindGroup &&
                   m_whaleNodeBindGroup && m_whaleSkinBindGroup;
        }

        bool CreateGltfPipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayout layouts[4] = {
                m_cameraBindGroupLayout,
                m_generalBindGroupLayout,
                m_whaleNodeBindGroupLayout,
                m_whaleSkinBindGroupLayout,
            };
            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"whale_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 4;
            plDesc.bindGroupLayouts = layouts;
            m_whalePipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_whalePipelineLayout)
                return false;

            WGPUVertexAttribute posAttr{};
            posAttr.format = WGPUVertexFormat_Float32x3;
            posAttr.offset = 0;
            posAttr.shaderLocation = 0;
            WGPUVertexAttribute nrmAttr{};
            nrmAttr.format = WGPUVertexFormat_Float32x3;
            nrmAttr.offset = 0;
            nrmAttr.shaderLocation = 1;
            WGPUVertexAttribute jntAttr{};
            jntAttr.format = m_whaleJointIsU16 ? WGPUVertexFormat_Uint16x4
                                               : WGPUVertexFormat_Uint8x4;
            jntAttr.offset = 0;
            jntAttr.shaderLocation = 2;
            WGPUVertexAttribute wgtAttr{};
            wgtAttr.format = WGPUVertexFormat_Float32x4;
            wgtAttr.offset = 0;
            wgtAttr.shaderLocation = 3;

            WGPUVertexBufferLayout layoutsArr[4]{};
            layoutsArr[0].arrayStride = 12;
            layoutsArr[0].stepMode = WGPUVertexStepMode_Vertex;
            layoutsArr[0].attributeCount = 1;
            layoutsArr[0].attributes = &posAttr;
            layoutsArr[1].arrayStride = 12;
            layoutsArr[1].stepMode = WGPUVertexStepMode_Vertex;
            layoutsArr[1].attributeCount = 1;
            layoutsArr[1].attributes = &nrmAttr;
            layoutsArr[2].arrayStride = m_whaleJointIsU16 ? 8u : 4u;
            layoutsArr[2].stepMode = WGPUVertexStepMode_Vertex;
            layoutsArr[2].attributeCount = 1;
            layoutsArr[2].attributes = &jntAttr;
            layoutsArr[3].arrayStride = 16;
            layoutsArr[3].stepMode = WGPUVertexStepMode_Vertex;
            layoutsArr[3].attributeCount = 1;
            layoutsArr[3].attributes = &wgtAttr;

            WGPUColorTargetState color{};
            color.format = ctx.surfaceFormat;
            color.writeMask = WGPUColorWriteMask_All;
            WGPUFragmentState fs{};
            fs.module = m_whalePixelShader;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &color;

            WGPUDepthStencilState depth{};
            depth.format = WGPUTextureFormat_Depth24Plus;
            depth.depthWriteEnabled = WGPUOptionalBool_True;
            depth.depthCompare = WGPUCompareFunction_Less;
            depth.stencilFront.compare = WGPUCompareFunction_Always;
            depth.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {"whale_pipe", WGPU_STRLEN};
            desc.layout = m_whalePipelineLayout;
            desc.vertex.module = m_whaleVertexShader;
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.vertex.bufferCount = 4;
            desc.vertex.buffers = layoutsArr;
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            // Upstream builds no cull mode (glTF default), material is double-sided.
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.depthStencil = &depth;
            desc.multisample.count = 1;
            desc.multisample.mask = 0xFFFFFFFF;
            desc.fragment = &fs;
            m_whalePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            return m_whalePipeline != nullptr;
        }

        // -------------------------------------------------------------------
        // Grid resource creation — gridUtils.ts.
        // -------------------------------------------------------------------
        bool CreateGridResources(pwgpu::test::SampleContext &ctx)
        {
            m_gridPositionBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(kGridVertices),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "grid_pos");
            m_gridJointsBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(kGridJoints),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "grid_jnt");
            m_gridWeightsBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(kGridWeights),
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, "grid_wgt");

            const uint64_t idxSize = (static_cast<uint64_t>(sizeof(kGridIndices)) + 3u) & ~uint64_t(3);
            m_gridIndexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, idxSize,
                WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, "grid_idx");
            if (!m_gridPositionBuffer || !m_gridJointsBuffer ||
                !m_gridWeightsBuffer || !m_gridIndexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_gridPositionBuffer, 0, kGridVertices, sizeof(kGridVertices));
            wgpuQueueWriteBuffer(ctx.queue, m_gridJointsBuffer, 0, kGridJoints, sizeof(kGridJoints));
            wgpuQueueWriteBuffer(ctx.queue, m_gridWeightsBuffer, 0, kGridWeights, sizeof(kGridWeights));
            std::vector<uint8_t> paddedIdx(static_cast<size_t>(idxSize), 0);
            std::memcpy(paddedIdx.data(), kGridIndices, sizeof(kGridIndices));
            wgpuQueueWriteBuffer(ctx.queue, m_gridIndexBuffer, 0, paddedIdx.data(), paddedIdx.size());

            const uint64_t boneBufSize = kMat4Bytes * kGridBones;
            m_gridJointBuffer = pwgpu::test::CreateBuffer(
                ctx.device, boneBufSize,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "grid_skin_jnt");
            m_gridInverseBindBuffer = pwgpu::test::CreateBuffer(
                ctx.device, boneBufSize,
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "grid_skin_ibm");
            if (!m_gridJointBuffer || !m_gridInverseBindBuffer)
                return false;

            // main.ts: bind-pose inverse via animSkinnedGrid(bindPoses, 0).
            // With angle=0 the rotation matrices are identity, so each bind-pose
            // is a translation chain [0, 4, 8, I, I]. Compute inverse explicitly.
            std::array<mat4, kGridBones> bindPoses{};
            bindPoses[0] = mat4(1.f);
            mat4 stepA = glm::translate(bindPoses[0], vec3(4.f, 0.f, 0.f));
            bindPoses[1] = stepA;
            mat4 stepB = glm::translate(bindPoses[1], vec3(4.f, 0.f, 0.f));
            bindPoses[2] = stepB;
            bindPoses[3] = mat4(1.f);
            bindPoses[4] = mat4(1.f);

            std::array<mat4, kGridBones> bindPosesInv{};
            for (uint32_t i = 0; i < kGridBones; ++i)
                bindPosesInv[i] = glm::inverse(bindPoses[i]);

            wgpuQueueWriteBuffer(ctx.queue, m_gridInverseBindBuffer, 0,
                                 bindPosesInv.data(), boneBufSize);

            // Bind group layout / pipeline for grid — reuses camera/general BGLs.
            WGPUBindGroupLayoutEntry skin[2] = {};
            skin[0].binding = 0;
            skin[0].visibility = WGPUShaderStage_Vertex;
            skin[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            skin[1].binding = 1;
            skin[1].visibility = WGPUShaderStage_Vertex;
            skin[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            WGPUBindGroupLayoutDescriptor skinDesc{};
            skinDesc.label = {"grid_skin_bgl", WGPU_STRLEN};
            skinDesc.entryCount = 2;
            skinDesc.entries = skin;
            m_gridSkinBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &skinDesc);
            if (!m_gridSkinBindGroupLayout)
                return false;

            WGPUBindGroupEntry bindEntries[2] = {};
            bindEntries[0].binding = 0;
            bindEntries[0].buffer = m_gridJointBuffer;
            bindEntries[0].size = boneBufSize;
            bindEntries[1].binding = 1;
            bindEntries[1].buffer = m_gridInverseBindBuffer;
            bindEntries[1].size = boneBufSize;
            WGPUBindGroupDescriptor bindDesc{};
            bindDesc.label = {"grid_skin_bg", WGPU_STRLEN};
            bindDesc.layout = m_gridSkinBindGroupLayout;
            bindDesc.entryCount = 2;
            bindDesc.entries = bindEntries;
            m_gridSkinBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bindDesc);
            if (!m_gridSkinBindGroup)
                return false;

            WGPUBindGroupLayout gridLayouts[3] = {
                m_cameraBindGroupLayout,
                m_generalBindGroupLayout,
                m_gridSkinBindGroupLayout,
            };
            WGPUPipelineLayoutDescriptor gridPlDesc{};
            gridPlDesc.label = {"grid_pl", WGPU_STRLEN};
            gridPlDesc.bindGroupLayoutCount = 3;
            gridPlDesc.bindGroupLayouts = gridLayouts;
            m_gridPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &gridPlDesc);
            if (!m_gridPipelineLayout)
                return false;

            WGPUVertexAttribute posAttr{};
            posAttr.format = WGPUVertexFormat_Float32x2;
            posAttr.offset = 0;
            posAttr.shaderLocation = 0;
            WGPUVertexAttribute jntAttr{};
            jntAttr.format = WGPUVertexFormat_Uint32x4;
            jntAttr.offset = 0;
            jntAttr.shaderLocation = 1;
            WGPUVertexAttribute wgtAttr{};
            wgtAttr.format = WGPUVertexFormat_Float32x4;
            wgtAttr.offset = 0;
            wgtAttr.shaderLocation = 2;
            WGPUVertexBufferLayout buffers[3]{};
            buffers[0].arrayStride = 2 * sizeof(float);
            buffers[0].stepMode = WGPUVertexStepMode_Vertex;
            buffers[0].attributeCount = 1;
            buffers[0].attributes = &posAttr;
            buffers[1].arrayStride = 4 * sizeof(uint32_t);
            buffers[1].stepMode = WGPUVertexStepMode_Vertex;
            buffers[1].attributeCount = 1;
            buffers[1].attributes = &jntAttr;
            buffers[2].arrayStride = 4 * sizeof(float);
            buffers[2].stepMode = WGPUVertexStepMode_Vertex;
            buffers[2].attributeCount = 1;
            buffers[2].attributes = &wgtAttr;

            WGPUColorTargetState color{};
            color.format = ctx.surfaceFormat;
            color.writeMask = WGPUColorWriteMask_All;
            WGPUFragmentState fs{};
            fs.module = m_gridPixelShader;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &color;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {"grid_pipe", WGPU_STRLEN};
            desc.layout = m_gridPipelineLayout;
            desc.vertex.module = m_gridVertexShader;
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.vertex.bufferCount = 3;
            desc.vertex.buffers = buffers;
            desc.primitive.topology = WGPUPrimitiveTopology_LineList;
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.multisample.count = 1;
            desc.multisample.mask = 0xFFFFFFFF;
            desc.fragment = &fs;
            // Grid pass has no depth attachment in upstream.
            desc.depthStencil = nullptr;
            m_gridPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            return m_gridPipeline != nullptr;
        }

        // -------------------------------------------------------------------
        // Node / skin update — walks the scene graph and rebuilds world and
        // joint matrices every frame.
        // -------------------------------------------------------------------
        void InitOrigMatrices()
        {
            m_origMatrices.clear();
            for (uint32_t joint : m_pack.skins[0].joints)
                m_origMatrices[joint] = ComposeTRS(m_pack.nodes[joint].transform);
        }

        void AnimateWhaleSkin(float angle)
        {
            // main.ts: for joints 0,1 rotateY(-angle); joints 3,4 rotateX(+/-angle);
            // others rotateZ(+angle). Writes TRS back onto the node.
            for (uint32_t joint : m_pack.skins[0].joints)
            {
                auto it = m_origMatrices.find(joint);
                if (it == m_origMatrices.end())
                    continue;
                mat4 m;
                if (joint == 1 || joint == 0)
                    m = glm::rotate(it->second, -angle, vec3(0.f, 1.f, 0.f));
                else if (joint == 3)
                    m = glm::rotate(it->second, angle, vec3(1.f, 0.f, 0.f));
                else if (joint == 4)
                    m = glm::rotate(it->second, -angle, vec3(1.f, 0.f, 0.f));
                else
                    m = glm::rotate(it->second, angle, vec3(0.f, 0.f, 1.f));

                // Decompose m back into TRS on the node — matches wgpu-matrix
                // mat4.getTranslation / getScaling / quat.fromMat.
                GlbNode &node = m_pack.nodes[joint];
                node.transform.position = {m[3].x, m[3].y, m[3].z};
                vec3 col0(m[0].x, m[0].y, m[0].z);
                vec3 col1(m[1].x, m[1].y, m[1].z);
                vec3 col2(m[2].x, m[2].y, m[2].z);
                node.transform.scale = {glm::length(col0), glm::length(col1), glm::length(col2)};
                mat4 rot(1.f);
                rot[0] = vec4(col0 / node.transform.scale[0], 0.f);
                rot[1] = vec4(col1 / node.transform.scale[1], 0.f);
                rot[2] = vec4(col2 / node.transform.scale[2], 0.f);
                glm::quat q = glm::quat_cast(rot);
                node.transform.rotation = {q.x, q.y, q.z, q.w};
            }
        }

        void UpdateNodeWorldMatrices(pwgpu::test::SampleContext &ctx)
        {
            // Walk each scene's roots; rely on scene.root being identity (matches
            // GLTFScene constructor — new BaseTransformation()).
            if (m_pack.scenes.empty())
                return;

            auto visit = [&](auto &self, uint32_t idx, const mat4 &parentWorld) -> void
            {
                GlbNode &n = m_pack.nodes[idx];
                n.localMatrix = ComposeTRS(n.transform);
                n.worldMatrix = parentWorld * n.localMatrix;
                for (uint32_t c : n.children)
                    self(self, c, n.worldMatrix);
            };

            const mat4 identity(1.f);
            for (const GlbScene &scene : m_pack.scenes)
            {
                for (uint32_t root : scene.rootNodes)
                    visit(visit, root, identity);
            }

            // Upload the mesh-holder node (node 6 = "orca") worldMatrix for the
            // whale uniform. main.ts passes 6 explicitly.
            constexpr uint32_t kMeshNode = 6;
            if (kMeshNode < m_pack.nodes.size())
            {
                wgpuQueueWriteBuffer(ctx.queue, m_whaleNodeBuffer, 0,
                                     glm::value_ptr(m_pack.nodes[kMeshNode].worldMatrix),
                                     kMat4Bytes);
            }
        }

        void UploadSkinJointMatrices(pwgpu::test::SampleContext &ctx)
        {
            // Skin.update: globalWorldInverse * joint[j].worldMatrix, where
            // globalWorldInverse == inverse(nodes[currentNodeIndex].worldMatrix).
            // main.ts passes currentNodeIndex = 6.
            constexpr uint32_t kMeshNode = 6;
            const mat4 globalInverse = glm::inverse(m_pack.nodes[kMeshNode].worldMatrix);
            const GlbSkin &skin = m_pack.skins[0];
            std::vector<mat4> scratch(skin.joints.size());
            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                const uint32_t jointIdx = skin.joints[j];
                scratch[j] = globalInverse * m_pack.nodes[jointIdx].worldMatrix;
            }
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_whaleJointMatrixBuffer,
                                 0,
                                 scratch.data(),
                                 scratch.size() * kMat4Bytes);
        }

        // -------------------------------------------------------------------
        // Rendering.
        // -------------------------------------------------------------------
        bool ExecuteWhalePass(pwgpu::test::SampleFrame &frame)
        {
            if (!m_whalePipeline || !m_depthView)
                return true;

            WGPURenderPassColorAttachment color{};
            color.view = frame.surfaceView;
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            color.clearValue = {0.3, 0.3, 0.3, 1.0};

            WGPURenderPassDepthStencilAttachment depth{};
            depth.view = m_depthView;
            depth.depthClearValue = 1.f;
            depth.depthLoadOp = WGPULoadOp_Clear;
            depth.depthStoreOp = WGPUStoreOp_Store;
            depth.depthReadOnly = WGPUOptionalBool_False;
            depth.stencilLoadOp = WGPULoadOp_Undefined;
            depth.stencilStoreOp = WGPUStoreOp_Undefined;
            depth.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderPassDescriptor pd{};
            pd.label = {"whale_pass", WGPU_STRLEN};
            pd.colorAttachmentCount = 1;
            pd.colorAttachments = &color;
            pd.depthStencilAttachment = &depth;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &pd);
            wgpuRenderPassEncoderSetPipeline(pass, m_whalePipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_cameraBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, m_generalBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 2, m_whaleNodeBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 3, m_whaleSkinBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_whalePositionBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_whaleNormalBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 2, m_whaleJointsBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 3, m_whaleWeightsBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, m_whaleIndexBuffer,
                                                WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, m_whaleIndexCount, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        bool ExecuteGridPass(pwgpu::test::SampleFrame &frame)
        {
            if (!m_gridPipeline)
                return true;

            WGPURenderPassColorAttachment color{};
            color.view = frame.surfaceView;
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            color.clearValue = {0.3, 0.3, 0.3, 1.0};

            WGPURenderPassDescriptor pd{};
            pd.label = {"grid_pass", WGPU_STRLEN};
            pd.colorAttachmentCount = 1;
            pd.colorAttachments = &color;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &pd);
            wgpuRenderPassEncoderSetPipeline(pass, m_gridPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_cameraBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, m_generalBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 2, m_gridSkinBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_gridPositionBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_gridJointsBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 2, m_gridWeightsBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, m_gridIndexBuffer,
                                                WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            const uint32_t indexCount = sizeof(kGridIndices) / sizeof(kGridIndices[0]);
            wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void ReleaseDepthTarget()
        {
            if (m_depthView)
                wgpuTextureViewRelease(m_depthView);
            if (m_depthTexture)
                wgpuTextureRelease(m_depthTexture);
            m_depthView = nullptr;
            m_depthTexture = nullptr;
        }

        // --- data ---
        Settings m_settings{};
        GlbScenePack m_pack{};
        std::unordered_map<uint32_t, mat4> m_origMatrices;
        std::array<mat4, kGridBones> m_gridBoneTransforms{};

        uint32_t m_whaleIndexCount = 0;
        bool m_whaleJointIsU16 = false;

        // shaders
        WGPUShaderModule m_whaleVertexShader = nullptr;
        WGPUShaderModule m_whalePixelShader = nullptr;
        WGPUShaderModule m_gridVertexShader = nullptr;
        WGPUShaderModule m_gridPixelShader = nullptr;

        // whale buffers
        WGPUBuffer m_whalePositionBuffer = nullptr;
        WGPUBuffer m_whaleNormalBuffer = nullptr;
        WGPUBuffer m_whaleJointsBuffer = nullptr;
        WGPUBuffer m_whaleWeightsBuffer = nullptr;
        WGPUBuffer m_whaleIndexBuffer = nullptr;
        WGPUBuffer m_whaleNodeBuffer = nullptr;
        WGPUBuffer m_whaleJointMatrixBuffer = nullptr;
        WGPUBuffer m_whaleInverseBindBuffer = nullptr;

        // grid buffers
        WGPUBuffer m_gridPositionBuffer = nullptr;
        WGPUBuffer m_gridJointsBuffer = nullptr;
        WGPUBuffer m_gridWeightsBuffer = nullptr;
        WGPUBuffer m_gridIndexBuffer = nullptr;
        WGPUBuffer m_gridJointBuffer = nullptr;
        WGPUBuffer m_gridInverseBindBuffer = nullptr;

        // shared
        WGPUBuffer m_cameraBuffer = nullptr;
        WGPUBuffer m_generalBuffer = nullptr;

        // bind group layouts
        WGPUBindGroupLayout m_cameraBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_generalBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_whaleNodeBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_whaleSkinBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_gridSkinBindGroupLayout = nullptr;

        // bind groups
        WGPUBindGroup m_cameraBindGroup = nullptr;
        WGPUBindGroup m_generalBindGroup = nullptr;
        WGPUBindGroup m_whaleNodeBindGroup = nullptr;
        WGPUBindGroup m_whaleSkinBindGroup = nullptr;
        WGPUBindGroup m_gridSkinBindGroup = nullptr;

        // pipelines
        WGPUPipelineLayout m_whalePipelineLayout = nullptr;
        WGPUPipelineLayout m_gridPipelineLayout = nullptr;
        WGPURenderPipeline m_whalePipeline = nullptr;
        WGPURenderPipeline m_gridPipeline = nullptr;

        // depth (whale only)
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeSkinnedMeshDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "SkinnedMeshTest";
        desc.windowTitle = "PhasmaWebGPU Skinned Mesh";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    SkinnedMeshSample sample;
    pwgpu::test::SampleApp app(sample, MakeSkinnedMeshDesc());
    return app.Run(argc, argv);
}
