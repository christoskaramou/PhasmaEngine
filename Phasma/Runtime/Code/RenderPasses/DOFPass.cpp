#include "DOFPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "RenderPasses/LightPass.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        constexpr uint32_t kTileSize = 16;       // DOF_TILE in DOF.hlsl, the prepass group size
        constexpr uint32_t kDilateGroupSize = 8; // DOFTileDilateCS numthreads

        // DOFParams in DOF.hlsl.
        struct DOFPushConstants
        {
            float maxRadius;
            float blend;
            float focusDistance;
            float nearPlane;
            float fStop;
            float focalLength;
            float sensorHeight;
            float imageHeight;
            uint32_t flags;
        };

        DOFPushConstants MakePushConstants(Image *displayRT, Image *transparentDepth)
        {
            DOFPushConstants pc{};
            pc.maxRadius = std::max(ActivePostProcessProfile().dof_blur_range, 0.5f);
            pc.blend = ActivePostProcessBlend().dof;
            pc.imageHeight = displayRT->GetHeight_f();

            OpenPBRForwardPass *forward = GetGlobalComponent<OpenPBRForwardPass>();
            if (transparentDepth && forward && forward->GetTransparentDepth() == transparentDepth)
                pc.flags |= 2u; // DOF_TRANSPARENT_DEPTH

            Scene *scene = GetActiveScene();
            Camera *camera = scene ? scene->GetActiveCamera() : nullptr;
            if (camera && !camera->IsOrthographic())
            {
                // Thin lens: the focal length that gives this vertical field of view on the camera's sensor.
                pc.sensorHeight = camera->GetSensorHeight() * 0.001f;
                pc.focalLength = 0.5f * pc.sensorHeight / std::tan(0.5f * camera->Fovy());
                pc.fStop = camera->GetFStop();
                pc.focusDistance = camera->GetFocusDistance();
                pc.nearPlane = camera->GetNearPlane();
                pc.flags |= 1u; // DOF_PERSPECTIVE
            }

            return pc;
        }

        uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1) / divisor;
        }

        Image *CreateStorageImage(const char *name, ::PeFormat format, uint32_t width, uint32_t height)
        {
            ImageDesc desc{};
            desc.format = format;
            desc.width = width;
            desc.height = height;
            desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE;
            desc.name = name;

            Image *image = Image::Create(desc);
            image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            image->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);
            return image;
        }

        void Barrier(CommandBuffer *cmd, Image *image, PeImageLayout layout, PeBarrierSync stage, PeBarrierAccess access)
        {
            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = layout;
            barrier.stageFlags = stage;
            barrier.accessMask = access;
            cmd->ImageBarrier(barrier);
        }

        void ReadBarrier(CommandBuffer *cmd, Image *image, PeBarrierSync stage)
        {
            Barrier(cmd, image, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stage, PE_ACCESS_SHADER_SAMPLED_READ);
        }

        void WriteBarrier(CommandBuffer *cmd, Image *image)
        {
            Barrier(cmd, image, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
        }

        std::shared_ptr<PassInfo> ComputePassInfo(std::shared_ptr<PassInfo> info, const char *name, const char *source)
        {
            if (!info)
                info = std::make_shared<PassInfo>();

            info->name = name;
            info->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + source,
                                                .entryPoint = "mainCS",
                                                .stage = PE_SHADER_STAGE_COMPUTE,
                                                .defines = std::vector<Define>{}});
            info->Update();
            return info;
        }

        void DestroyComputePassInfo(std::shared_ptr<PassInfo> &info)
        {
            if (!info)
                return;

            Shader::Destroy(info->pCompShader);
            info.reset();
        }
    } // namespace

    void DOFPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_displayRT = rs->GetRenderTarget("hdrDisplay");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_transparentDepth = rs->GetRenderTarget("transparentDepth"); // OpenPBR scenes only
        m_frameImage = rs->CreateFSSampledImage(m_displayRT);

        // Pass-owned, at the display's size: the frame packed with inverse depth, and each tile's largest blur before
        // and after spreading it over the blur cap.
        const uint32_t width = m_displayRT->GetWidth();
        const uint32_t height = m_displayRT->GetHeight();
        const uint32_t tilesX = DivideRoundUp(width, kTileSize);
        const uint32_t tilesY = DivideRoundUp(height, kTileSize);
        m_packed = CreateStorageImage("dofPacked", PE_FORMAT_R16G16B16A16_SFLOAT, width, height);
        m_tiles = CreateStorageImage("dofTiles", PE_FORMAT_R32_SFLOAT, tilesX, tilesY);
        m_reach = CreateStorageImage("dofReach", PE_FORMAT_R32_SFLOAT, tilesX, tilesY);

        // Loaded: the gather writes colour only, and the display keeps its alpha.
        m_attachments.resize(1);
        m_attachments[0] = {};
        m_attachments[0].image = m_displayRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
    }

    void DOFPass::UpdatePassInfo()
    {
        m_prepassInfo = ComputePassInfo(m_prepassInfo, "dof_prepass_pipeline", "Shaders/DepthOfField/DOFPrepassCS.hlsl");
        m_dilateInfo = ComputePassInfo(m_dilateInfo, "dof_dilate_pipeline", "Shaders/DepthOfField/DOFTileDilateCS.hlsl");

        m_passInfo->name = "dof_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Common/Quad.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/DepthOfField/DOFPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->cullMode = PE_CULL_MODE_NONE;

        // Colour only: the packed frame's alpha holds depth, and the display target keeps its own.
        m_passInfo->colorBlendAttachments = {BlendState(false,
                                                        PE_BLEND_FACTOR_ONE,
                                                        PE_BLEND_FACTOR_ZERO,
                                                        PE_BLEND_OP_ADD,
                                                        PE_BLEND_FACTOR_ONE,
                                                        PE_BLEND_FACTOR_ZERO,
                                                        PE_BLEND_OP_ADD,
                                                        PE_COLOR_COMPONENT_R | PE_COLOR_COMPONENT_G | PE_COLOR_COMPONENT_B)};
        m_passInfo->colorFormats = {m_displayRT->GetFormat()};
        m_passInfo->depthTestEnable = false;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->stencilTestEnable = false;
        m_passInfo->Update();
    }

    void DOFPass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void DOFPass::UpdateDescriptorSets()
    {
        if (!m_prepassInfo || !m_dilateInfo)
            return;

        Image *transparentDepth = m_transparentDepth ? m_transparentDepth : m_depth;
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            Descriptor *prepass = m_prepassInfo->GetDescriptors(i)[0];
            prepass->SetImageView(0, m_frameImage->GetSRV());
            prepass->SetImageView(1, m_depth->GetSRV());
            prepass->SetImageView(2, transparentDepth->GetSRV());
            prepass->SetImageView(3, m_packed->GetUAV(0));
            prepass->SetImageView(4, m_tiles->GetUAV(0));
            prepass->Update();

            Descriptor *dilate = m_dilateInfo->GetDescriptors(i)[0];
            dilate->SetImageView(0, m_tiles->GetUAV(0));
            dilate->SetImageView(1, m_reach->GetUAV(0));
            dilate->Update();

            Descriptor *gather = m_passInfo->GetDescriptors(i)[0];
            gather->SetImageView(0, m_packed->GetSRV());
            gather->SetImageView(1, m_reach->GetSRV());
            gather->Update();
        }
    }

    void DOFPass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_depth);
    }

    void DOFPass::ExecutePass(CommandBuffer *cmd)
    {
        const DOFPushConstants pc = MakePushConstants(m_displayRT, m_transparentDepth);
        const uint32_t tilesX = m_tiles->GetWidth();
        const uint32_t tilesY = m_tiles->GetHeight();

        cmd->BeginDebugRegion("DOFPass");
        cmd->CopyImage(m_displayRT, m_frameImage);

        // Prepass. transparentDepth is not a graph read: its only writer, OpenPBRForwardPass, leaves the graph in
        // scenes without forward meshes, while the descriptor still needs the image readable.
        ReadBarrier(cmd, m_frameImage, PE_STAGE_COMPUTE_SHADER);
        if (m_transparentDepth)
            ReadBarrier(cmd, m_transparentDepth, PE_STAGE_COMPUTE_SHADER);

        WriteBarrier(cmd, m_packed);
        WriteBarrier(cmd, m_tiles);
        cmd->BindPipeline(*m_prepassInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Dispatch(tilesX, tilesY, 1);

        // Dilate: the tiles stay storage images, so a memory barrier orders the read after the prepass's writes.
        MemoryBarrierInfo tilesWritten{};
        tilesWritten.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        tilesWritten.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        tilesWritten.dstStageMask = PE_STAGE_COMPUTE_SHADER;
        tilesWritten.dstAccessMask = PE_ACCESS_SHADER_READ;
        cmd->MemoryBarrier(tilesWritten);

        WriteBarrier(cmd, m_reach);
        cmd->BindPipeline(*m_dilateInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Dispatch(DivideRoundUp(tilesX, kDilateGroupSize), DivideRoundUp(tilesY, kDilateGroupSize), 1);

        // Gather into the display target.
        ReadBarrier(cmd, m_packed, PE_STAGE_FRAGMENT_SHADER);
        ReadBarrier(cmd, m_reach, PE_STAGE_FRAGMENT_SHADER);
        cmd->BeginPass(1, m_attachments.data(), "DOF");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetViewport(0.f, 0.f, m_displayRT->GetWidth_f(), m_displayRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_displayRT->GetWidth(), m_displayRT->GetHeight());
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void DOFPass::Resize(uint32_t width, uint32_t height)
    {
        DestroyImages();
        Init();
        UpdateDescriptorSets();
    }

    void DOFPass::Destroy()
    {
        DestroyImages();
        DestroyComputePassInfo(m_prepassInfo);
        DestroyComputePassInfo(m_dilateInfo);
    }

    std::vector<PassInfo *> DOFPass::GetPassInfos() noexcept
    {
        std::vector<PassInfo *> passInfos{m_passInfo.get()};
        if (m_prepassInfo)
            passInfos.push_back(m_prepassInfo.get());
        if (m_dilateInfo)
            passInfos.push_back(m_dilateInfo.get());

        return passInfos;
    }

    void DOFPass::DestroyImages()
    {
        Image::Destroy(m_frameImage);
        Image::Destroy(m_packed);
        Image::Destroy(m_tiles);
        Image::Destroy(m_reach);
    }
} // namespace pe
