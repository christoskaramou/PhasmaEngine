#include "Render/ScenePerception.h"

#include "Scene/Scene.h"
#include "Camera/Camera.h"
#include "RenderPasses/DepthPass.h" // PushConstants_DepthPass
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    namespace
    {
        // CPU mirror of the Perception/NodeVis HLSL struct (24 bytes).
        struct ObjectIdNodeVis
        {
            uint32_t pixelCount;
            uint32_t minX;
            uint32_t minY;
            uint32_t maxX;
            uint32_t maxY;
            uint32_t nearestDepthBits;
        };
        static_assert(sizeof(ObjectIdNodeVis) == 24, "ObjectIdNodeVis must match Perception NodeVis");

        struct PushConstants_ObjReduce
        {
            uint32_t width;
            uint32_t height;
            uint32_t drawCount;
            uint32_t pad0;
        };

        // Lazily-built, cached GPU resources reused across decode/pick calls. Rebuilt when the
        // viewport, draw capacity, depth format or reverse-depth mode changes.
        struct ObjectIdDecodeCache
        {
            Image *idTarget = nullptr;
            Buffer *nodeVis = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t nodeVisCapacity = 0;
            ::PeFormat depthFormat = PE_FORMAT_UNDEFINED;
            bool reverseDepth = false;
            std::unique_ptr<PassInfo> idPI;
            std::unique_ptr<PassInfo> clearPI;
            std::unique_ptr<PassInfo> reducePI;
        };

        ObjectIdDecodeCache &GetObjectIdDecodeCache()
        {
            static ObjectIdDecodeCache cache;
            return cache;
        }

        uint32_t NextPow2(uint32_t value)
        {
            if (value <= 1)
                return 1;
            value--;
            value |= value >> 1;
            value |= value >> 2;
            value |= value >> 4;
            value |= value >> 8;
            value |= value >> 16;
            return value + 1;
        }

        float FloatFromBits(uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        // Reverse-Z infinite-projection NDC depth -> camera-space distance. Matches Camera.cpp's
        // m_projection setup (ndc.z = near / viewZ for the infinite-far perspective case).
        float DecodeCameraDepthDistance(float ndcDepth, const Camera &camera)
        {
            if (!std::isfinite(ndcDepth))
                return std::numeric_limits<float>::infinity();

            const float nearPlane = std::max(camera.GetNearPlane(), 1e-6f);
            const float farPlane = camera.GetFarPlane();
            const float depth = std::clamp(ndcDepth, 0.0f, 1.0f);

            if (camera.IsOrthographic())
            {
                if (std::isfinite(farPlane) && farPlane > nearPlane)
                {
                    const bool reverseDepth = Settings::Get<SceneSettings>().reverse_depth;
                    return reverseDepth ? nearPlane + (1.0f - depth) * (farPlane - nearPlane)
                                        : nearPlane + depth * (farPlane - nearPlane);
                }
                return 0.0f;
            }

            if (depth <= 1e-8f)
                return std::numeric_limits<float>::infinity();

            if (!std::isfinite(farPlane) || farPlane > std::numeric_limits<float>::max() * 0.5f)
                return nearPlane / depth;

            return (nearPlane * farPlane) / std::max(nearPlane + depth * (farPlane - nearPlane), 1e-8f);
        }

        BufferBarrierInfo MakeBufferBarrier(Buffer *buffer, PeBarrierSync stage, PeBarrierAccess access, size_t size)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = stage;
            barrier.accessMask = access;
            barrier.offset = 0;
            barrier.size = size;
            return barrier;
        }

        void RecordBufferState(Buffer *buffer, PeBarrierSync stage, PeBarrierAccess access, size_t size)
        {
            BufferTrackInfo &track = buffer->GetTrackInfo();
            track.buffer = buffer;
            track.stageMask = stage;
            track.accessMask = access;
            track.offset = 0;
            track.size = size;
        }

        std::unique_ptr<PassInfo> CreateObjectIdPassInfo(::PeFormat depthFormat, bool reverseDepth)
        {
            auto passInfo = std::make_unique<PassInfo>();
            passInfo->name = "ObjectIdDecode_pipeline";
            passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl",
                                                    .entryPoint = "mainVS",
                                                    .stage = PE_SHADER_STAGE_VERTEX,
                                                    .defines = std::vector<Define>{}});
            passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Perception/ObjectIdPS.hlsl",
                                                    .entryPoint = "mainPS",
                                                    .stage = PE_SHADER_STAGE_FRAGMENT,
                                                    .defines = std::vector<Define>{}});
            passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            passInfo->cullMode = PE_CULL_MODE_NONE;
            passInfo->colorBlendAttachments = {BlendState::Default};
            passInfo->colorFormats = {PE_FORMAT_R32G32_UINT};
            passInfo->depthFormat = depthFormat;
            passInfo->depthTestEnable = true;
            passInfo->depthWriteEnable = false;
            passInfo->depthCompareOp = reverseDepth ? PE_COMPARE_OP_GREATER_OR_EQUAL : PE_COMPARE_OP_LESS_OR_EQUAL;
            passInfo->Update();
            return passInfo;
        }

        std::unique_ptr<PassInfo> CreateObjectIdComputePassInfo(const std::string &name,
                                                                const std::string &shaderPath,
                                                                const std::string &entryPoint)
        {
            auto passInfo = std::make_unique<PassInfo>();
            passInfo->name = name;
            passInfo->pCompShader = Shader::Create({.sourcePath = shaderPath,
                                                    .entryPoint = entryPoint,
                                                    .stage = PE_SHADER_STAGE_COMPUTE,
                                                    .defines = std::vector<Define>{}});
            passInfo->Update();
            return passInfo;
        }

        bool EnsureObjectIdDecodeResources(ObjectIdDecodeCache &cache, Image *depth, uint32_t meshCount, std::string &outError)
        {
            if (!depth)
            {
                outError = "depth target not available";
                return false;
            }

            const uint32_t width = depth->GetWidth();
            const uint32_t height = depth->GetHeight();
            if (width == 0 || height == 0)
            {
                outError = "depth target has zero extent";
                return false;
            }

            if (!cache.idTarget || cache.width != width || cache.height != height)
            {
                Image::Destroy(cache.idTarget);
                cache.width = width;
                cache.height = height;

                cache.idTarget = Image::Create({
                    .width = width,
                    .height = height,
                    .format = PE_FORMAT_R32G32_UINT,
                    .usage = PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_SRC,
                    .name = "ObjectIdDecodeTarget",
                });
                cache.idTarget->CreateRTV();
                cache.idTarget->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            }

            const uint32_t requiredCapacity = NextPow2(meshCount);
            if (!cache.nodeVis || cache.nodeVisCapacity < requiredCapacity)
            {
                Buffer::Destroy(cache.nodeVis);
                cache.nodeVisCapacity = requiredCapacity;
                cache.nodeVis = Buffer::Create({
                    .size = static_cast<size_t>(cache.nodeVisCapacity) * sizeof(ObjectIdNodeVis),
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_SRC,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                    .name = "ObjectIdNodeVis",
                });
            }

            const ::PeFormat depthFormat = RHII.GetDepthFormat();
            const bool reverseDepth = Settings::Get<SceneSettings>().reverse_depth;
            if (!cache.idPI || cache.depthFormat != depthFormat || cache.reverseDepth != reverseDepth)
            {
                cache.idPI = CreateObjectIdPassInfo(depthFormat, reverseDepth);
                cache.depthFormat = depthFormat;
                cache.reverseDepth = reverseDepth;
            }
            if (!cache.clearPI)
                cache.clearPI = CreateObjectIdComputePassInfo("ObjectIdClear_pipeline",
                                                              Path::RuntimeAssets + "Shaders/Perception/ObjectIdClearCS.hlsl",
                                                              "clearCS");
            if (!cache.reducePI)
                cache.reducePI = CreateObjectIdComputePassInfo("ObjectIdReduce_pipeline",
                                                               Path::RuntimeAssets + "Shaders/Perception/ObjectIdReduceCS.hlsl",
                                                               "reduceCS");

            return true;
        }

        // Bind set0 (scene uniforms + mesh constants) and set1 (constants + sampler + textures) for
        // the id pass exactly like the depth prepass. Returns false if the descriptor layout is wrong.
        bool BindObjectIdDescriptors(ObjectIdDecodeCache &cache, Scene &scene, uint32_t frame)
        {
            const auto &idSets = cache.idPI->GetDescriptors(frame);
            if (idSets.size() < 2)
                return false;

            Descriptor *idSetUniforms = idSets[0];
            Descriptor *idSetTextures = idSets[1];
            idSetUniforms->SetBuffer(0, scene.GetUniforms(frame));
            idSetUniforms->SetBuffer(1, scene.GetMeshConstants());
            idSetUniforms->Update();
            idSetTextures->SetBuffer(0, scene.GetMeshConstants());
            idSetTextures->SetSampler(1, scene.GetDefaultSampler());
            idSetTextures->SetImageViews(2, scene.GetImageViews());
            idSetTextures->Update();
            return true;
        }

        // Record the object-id raster pass (reuses the live GPU-culled indirect draws) into `cmd`.
        void RecordObjectIdPass(CommandBuffer *cmd, ObjectIdDecodeCache &cache, Scene &scene, Image *depth, uint32_t frame, const char *passName)
        {
            const uint32_t meshCount = scene.GetMeshCount();

            Attachment attachments[2]{};
            attachments[0].image = cache.idTarget;
            attachments[0].loadOp = PE_LOAD_OP_CLEAR;
            attachments[0].storeOp = PE_STORE_OP_STORE;
            attachments[1].image = depth;
            attachments[1].loadOp = PE_LOAD_OP_LOAD;
            attachments[1].storeOp = PE_STORE_OP_STORE;
            cache.idTarget->SetClearColor(vec4(0.0f));

            PushConstants_DepthPass idConstants{};
            idConstants.jointsCount = static_cast<uint32_t>(scene.GetMaxJointCount());

            cmd->BeginPass(2, attachments, passName);
            cmd->SetViewport(0.0f, 0.0f, depth->GetWidth_f(), depth->GetHeight_f());
            cmd->SetScissor(0, 0, depth->GetWidth(), depth->GetHeight());
            cmd->BindPipeline(*cache.idPI);
            cmd->BindIndexBuffer(scene.GetBuffer(), 0);
            cmd->BindVertexBuffer(scene.GetBuffer(), scene.GetPositionsOffset());
            cmd->SetConstants(idConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirectCount(scene.GetIndirectOpaqueSS(frame), 0, scene.GetCullingCountersBuffer(frame), 0 * sizeof(uint32_t), meshCount);
            cmd->DrawIndexedIndirectCount(scene.GetIndirectAlphaCutSS(frame), 0, scene.GetCullingCountersBuffer(frame), 1 * sizeof(uint32_t), meshCount);
            cmd->DrawIndexedIndirectCount(scene.GetIndirectOpaqueDS(frame), 0, scene.GetCullingCountersBuffer(frame), 5 * sizeof(uint32_t), meshCount);
            cmd->DrawIndexedIndirectCount(scene.GetIndirectAlphaCutDS(frame), 0, scene.GetCullingCountersBuffer(frame), 6 * sizeof(uint32_t), meshCount);
            cmd->EndPass();
        }

        bool HaveSceneDrawBuffers(Scene &scene, uint32_t frame)
        {
            return scene.GetBuffer() && scene.GetUniforms(frame) && scene.GetMeshConstants() &&
                   scene.GetCullingCountersBuffer(frame) && scene.GetIndirectOpaqueSS(frame) &&
                   scene.GetIndirectAlphaCutSS(frame) && scene.GetIndirectOpaqueDS(frame) &&
                   scene.GetIndirectAlphaCutDS(frame);
        }
    } // namespace

    CameraViewResult DecodeCameraView(Scene &scene, Image *depth, uint32_t minPixels)
    {
        CameraViewResult out;

        Camera *camera = scene.GetActiveCamera();
        if (!camera)
        {
            out.error = "active camera not available";
            return out;
        }
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            out.error = "main queue not available";
            return out;
        }
        if (!depth)
        {
            out.error = "depth target not available";
            return out;
        }

        // Settle all in-flight frame work before recording the one-off pass: the depth buffer and
        // per-frame culled-indirect buffers must be complete, and EnsureObjectIdDecodeResources may
        // destroy/recreate the cached target. A static-camera decode can only under-report on a
        // frame mismatch (GEQUAL depth test drops stale draws), never corrupt.
        RHII.WaitDeviceIdle();

        out.cameraPosition = camera->GetPosition();
        out.viewProjection = camera->GetViewProjection();
        out.width = depth->GetWidth();
        out.height = depth->GetHeight();

        const uint32_t meshCount = scene.GetMeshCount();
        if (meshCount == 0)
        {
            out.valid = true; // empty scene: no visible nodes
            return out;
        }

        ObjectIdDecodeCache &cache = GetObjectIdDecodeCache();
        std::string error;
        if (!EnsureObjectIdDecodeResources(cache, depth, meshCount, error))
        {
            out.error = error;
            return out;
        }

        const uint32_t frame = RHII.GetFrameIndex();
        if (!HaveSceneDrawBuffers(scene, frame))
        {
            out.error = "scene draw buffers not available";
            return out;
        }
        if (!BindObjectIdDescriptors(cache, scene, frame))
        {
            out.error = "object-id descriptor layout missing expected sets";
            return out;
        }

        const auto &clearSets = cache.clearPI->GetDescriptors(frame);
        const auto &reduceSets = cache.reducePI->GetDescriptors(frame);
        if (clearSets.empty() || reduceSets.empty())
        {
            out.error = "object-id compute descriptor layout missing expected sets";
            return out;
        }

        clearSets[0]->SetBuffer(0, cache.nodeVis);
        clearSets[0]->Update();
        reduceSets[0]->SetImageView(0, cache.idTarget->GetSRV());
        reduceSets[0]->SetBuffer(1, cache.nodeVis);
        reduceSets[0]->Update();

        const size_t readbackSize = static_cast<size_t>(meshCount) * sizeof(ObjectIdNodeVis);
        Buffer *staging = Buffer::Create({
            .size = readbackSize,
            .usage = PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
            .name = "ObjectIdReadback",
        });

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        RecordObjectIdPass(cmd, cache, scene, depth, frame, "ObjectIdPass");

        ImageBarrierInfo idReadBarrier{};
        idReadBarrier.image = cache.idTarget;
        idReadBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        idReadBarrier.stageFlags = PE_STAGE_COMPUTE_SHADER;
        idReadBarrier.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_SAMPLED_READ;
        cmd->ImageBarrier(idReadBarrier);

        const size_t nodeVisSize = static_cast<size_t>(cache.nodeVisCapacity) * sizeof(ObjectIdNodeVis);
        cmd->BufferBarrier(MakeBufferBarrier(cache.nodeVis, PE_STAGE_COMPUTE_SHADER,
                                             PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE, nodeVisSize));

        PushConstants_ObjReduce reduceConstants{};
        reduceConstants.width = depth->GetWidth();
        reduceConstants.height = depth->GetHeight();
        reduceConstants.drawCount = meshCount;
        reduceConstants.pad0 = 0;

        cmd->BindPipeline(*cache.clearPI);
        cmd->SetConstants(reduceConstants);
        cmd->PushConstants();
        cmd->Dispatch((meshCount + 63) / 64, 1, 1);

        RecordBufferState(cache.nodeVis, PE_STAGE_COMPUTE_SHADER,
                          PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE, nodeVisSize);
        cmd->BufferBarrier(MakeBufferBarrier(cache.nodeVis, PE_STAGE_COMPUTE_SHADER,
                                             PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE |
                                                 PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE,
                                             nodeVisSize));

        cmd->BindPipeline(*cache.reducePI);
        cmd->SetConstants(reduceConstants);
        cmd->PushConstants();
        cmd->Dispatch((depth->GetWidth() + 7) / 8, (depth->GetHeight() + 7) / 8, 1);

        RecordBufferState(cache.nodeVis, PE_STAGE_COMPUTE_SHADER,
                          PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE, nodeVisSize);
        cmd->BufferBarrier(MakeBufferBarrier(cache.nodeVis, PE_STAGE_TRANSFER, PE_ACCESS_TRANSFER_READ, readbackSize));
        cmd->CopyBuffer(cache.nodeVis, staging, readbackSize, 0, 0);

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        queue->ReturnCommandBuffer(cmd);

        std::vector<ObjectIdNodeVis> vis(meshCount);
        staging->Map();
        if (const auto *src = static_cast<const ObjectIdNodeVis *>(staging->Data()))
            std::memcpy(vis.data(), src, readbackSize);
        staging->Unmap();
        Buffer::Destroy(staging);

        const std::vector<int> drawToNode = scene.BuildDrawIndexToNodeIndex();

        struct NodeAccum
        {
            uint64_t visiblePixels = 0;
            uint32_t minX = std::numeric_limits<uint32_t>::max();
            uint32_t minY = std::numeric_limits<uint32_t>::max();
            uint32_t maxX = 0;
            uint32_t maxY = 0;
            uint32_t nearestDepthBits = 0;
        };

        std::unordered_map<int, NodeAccum> byNode;
        for (uint32_t draw = 0; draw < meshCount; draw++)
        {
            if (draw >= drawToNode.size())
                break;
            const ObjectIdNodeVis &v = vis[draw];
            if (v.pixelCount == 0 || drawToNode[draw] < 0)
                continue;

            NodeAccum &acc = byNode[drawToNode[draw]];
            acc.visiblePixels += v.pixelCount;
            acc.minX = std::min(acc.minX, v.minX);
            acc.minY = std::min(acc.minY, v.minY);
            acc.maxX = std::max(acc.maxX, v.maxX);
            acc.maxY = std::max(acc.maxY, v.maxY);
            acc.nearestDepthBits = std::max(acc.nearestDepthBits, v.nearestDepthBits);
        }

        out.nodes.reserve(byNode.size());
        for (const auto &[nodeIndex, acc] : byNode)
        {
            if (acc.visiblePixels < static_cast<uint64_t>(minPixels) ||
                nodeIndex < 0 || nodeIndex >= static_cast<int>(scene.GetNodeCount()))
            {
                continue;
            }

            const float ndcDepth = FloatFromBits(acc.nearestDepthBits);
            CameraViewNodeVis node;
            node.nodeIndex = nodeIndex;
            node.visiblePixels = acc.visiblePixels;
            node.minX = acc.minX;
            node.minY = acc.minY;
            node.maxX = acc.maxX;
            node.maxY = acc.maxY;
            node.nearestNdcDepth = ndcDepth;
            node.distance = DecodeCameraDepthDistance(ndcDepth, *camera);
            out.nodes.push_back(node);
        }

        std::sort(out.nodes.begin(), out.nodes.end(), [](const CameraViewNodeVis &a, const CameraViewNodeVis &b)
                  { return a.visiblePixels > b.visiblePixels; });

        out.valid = true;
        return out;
    }

    CameraPickResult PickCameraPixel(Scene &scene, Image *depth, int reqX, int reqY, float groundY)
    {
        CameraPickResult out;

        Camera *camera = scene.GetActiveCamera();
        if (!camera)
        {
            out.error = "active camera not available";
            return out;
        }
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            out.error = "main queue not available";
            return out;
        }
        if (!depth)
        {
            out.error = "depth target not available";
            return out;
        }

        RHII.WaitDeviceIdle(); // see DecodeCameraView: settle in-flight frame work before recording

        const uint32_t meshCount = scene.GetMeshCount();
        ObjectIdDecodeCache &cache = GetObjectIdDecodeCache();
        std::string error;
        if (!EnsureObjectIdDecodeResources(cache, depth, std::max(meshCount, 1u), error))
        {
            out.error = error;
            return out;
        }

        const uint32_t W = depth->GetWidth();
        const uint32_t H = depth->GetHeight();
        out.width = W;
        out.height = H;
        if (reqX < 0 || reqY < 0 || reqX >= static_cast<int>(W) || reqY >= static_cast<int>(H))
        {
            out.error = "pixel out of range";
            return out;
        }

        const uint32_t frame = RHII.GetFrameIndex();
        const bool haveDraws = meshCount > 0 && HaveSceneDrawBuffers(scene, frame);

        uint32_t texelId = 0;    // .x = draw id + 1 (0 == empty)
        uint32_t texelDepth = 0; // .y = asuint(ndc depth)

        if (haveDraws)
        {
            if (!BindObjectIdDescriptors(cache, scene, frame))
            {
                out.error = "object-id descriptor layout missing expected sets";
                return out;
            }

            const size_t bpp = 8; // PE_FORMAT_R32G32_UINT
            const size_t rowBytes = static_cast<size_t>(W) * bpp;
            const size_t rowPitch = RHII.GetApi() == PE_GRAPHICS_API_DX12 ? RHII.AlignTextureRowPitch(rowBytes) : rowBytes;
            const size_t bufferSize = rowPitch * H;

            Buffer *staging = Buffer::Create({
                .size = bufferSize,
                .usage = PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
                .name = "ObjectIdPickReadback",
            });

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            RecordObjectIdPass(cmd, cache, scene, depth, frame, "ObjectIdPickPass");
            // CopyImageToBuffer inserts its own COLOR_ATTACHMENT -> TRANSFER_SRC barrier internally
            // (Vulkan Image::CopyToBuffer / DX12 TransitionForCopy), so no explicit barrier needed.
            cmd->CopyImageToBuffer(cache.idTarget, staging, 0, 0, 1);
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);

            staging->Map();
            if (const auto *base = static_cast<const uint8_t *>(staging->Data()))
            {
                const uint8_t *texelPtr = base + static_cast<size_t>(reqY) * rowPitch + static_cast<size_t>(reqX) * bpp;
                std::memcpy(&texelId, texelPtr, sizeof(uint32_t));
                std::memcpy(&texelDepth, texelPtr + sizeof(uint32_t), sizeof(uint32_t));
            }
            staging->Unmap();
            Buffer::Destroy(staging);
        }

        const mat4 invVP = camera->GetInvViewProjection();
        auto unproject = [&](float nx, float ny, float nz) -> vec3
        {
            vec4 p = invVP * vec4(nx, ny, nz, 1.0f);
            const float w = (std::abs(p.w) > 1e-12f) ? p.w : 1.0f;
            return vec3(p.x / w, p.y / w, p.z / w);
        };

        const float ndcX = 2.0f * (static_cast<float>(reqX) + 0.5f) / static_cast<float>(W) - 1.0f;
        const float ndcY = 2.0f * (static_cast<float>(reqY) + 0.5f) / static_cast<float>(H) - 1.0f;

        if (texelId != 0)
        {
            const uint32_t drawId = texelId - 1u;
            const std::vector<int> drawToNode = scene.BuildDrawIndexToNodeIndex();
            const int nodeIndex = (drawId < drawToNode.size()) ? drawToNode[drawId] : -1;

            const float ndcDepth = FloatFromBits(texelDepth);
            out.hit = true;
            out.ndcDepth = ndcDepth;
            out.worldHit = unproject(ndcX, ndcY, ndcDepth);
            out.distance = DecodeCameraDepthDistance(ndcDepth, *camera);
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(scene.GetNodeCount()))
                out.nodeIndex = nodeIndex;
        }
        else
        {
            // Miss (sky/empty): intersect the camera ray through the pixel with Y = groundY.
            const bool reverseDepth = Settings::Get<SceneSettings>().reverse_depth;
            const float farNdc = reverseDepth ? 0.0f : 1.0f;
            const vec3 camPos = camera->GetPosition();
            vec3 dir = unproject(ndcX, ndcY, farNdc) - camPos;
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 1e-8f)
            {
                dir /= len;
                if (std::abs(dir.y) > 1e-6f)
                {
                    const float t = (groundY - camPos.y) / dir.y;
                    if (t > 0.0f)
                    {
                        out.hasGroundPoint = true;
                        out.groundPoint = vec3(camPos.x + dir.x * t, groundY, camPos.z + dir.z * t);
                    }
                }
            }
        }

        out.valid = true;
        return out;
    }
} // namespace pe
