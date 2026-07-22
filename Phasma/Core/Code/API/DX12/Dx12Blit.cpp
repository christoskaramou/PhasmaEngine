#include "API/DX12/Dx12Blit.h"

#if defined(PE_WIN32)

#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12RootSignature.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Sampler.h"

namespace pe
{
    namespace
    {
#include "API/DX12/Dx12BlitShaders.inl"

        struct BlitGeometry
        {
            Offset3D srcMin{};
            Offset3D srcMax{};
            Offset3D dstMin{};
            Offset3D dstMax{};
            int32_t srcW = 0;
            int32_t srcH = 0;
            int32_t srcD = 0;
            int32_t dstW = 0;
            int32_t dstH = 0;
            int32_t dstD = 0;
            int32_t srcBack = 0;
            int32_t dstBack = 0;
        };

        BlitGeometry CalculateBlitGeometry(const ImageBlit &region)
        {
            BlitGeometry geometry{};
            geometry.srcMin = region.srcOffsets[0];
            geometry.srcMax = region.srcOffsets[1];
            geometry.dstMin = region.dstOffsets[0];
            geometry.dstMax = region.dstOffsets[1];
            geometry.srcBack = std::max(geometry.srcMax.z, geometry.srcMin.z + 1);
            geometry.dstBack = std::max(geometry.dstMax.z, geometry.dstMin.z + 1);
            geometry.srcW = geometry.srcMax.x - geometry.srcMin.x;
            geometry.srcH = geometry.srcMax.y - geometry.srcMin.y;
            geometry.srcD = geometry.srcBack - geometry.srcMin.z;
            geometry.dstW = geometry.dstMax.x - geometry.dstMin.x;
            geometry.dstH = geometry.dstMax.y - geometry.dstMin.y;
            geometry.dstD = geometry.dstBack - geometry.dstMin.z;
            return geometry;
        }

        bool IsScalingBlit(const BlitGeometry &geometry)
        {
            return geometry.srcW != geometry.dstW ||
                   geometry.srcH != geometry.dstH ||
                   geometry.srcD != geometry.dstD;
        }

        bool LayersOverlap(const ImageSubresourceLayers &a, const ImageSubresourceLayers &b)
        {
            const uint32_t aEnd = a.baseArrayLayer + (a.layerCount ? a.layerCount : 1u);
            const uint32_t bEnd = b.baseArrayLayer + (b.layerCount ? b.layerCount : 1u);
            return a.baseArrayLayer < bEnd && b.baseArrayLayer < aEnd;
        }

        bool OverlapsSubresource(const ImageBlit &region)
        {
            return region.srcSubresource.mipLevel == region.dstSubresource.mipLevel &&
                   LayersOverlap(region.srcSubresource, region.dstSubresource);
        }

        UINT SubresourceIndex(const Image *image, uint32_t arrayLayer, uint32_t mipLevel)
        {
            return mipLevel + arrayLayer * image->GetMipLevels();
        }

        uint32_t MipExtent(uint32_t extent, uint32_t mipLevel)
        {
            return std::max(extent >> mipLevel, 1u);
        }

        bool IsIntegerFormat(DXGI_FORMAT format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R8_SINT:
            case DXGI_FORMAT_R8_UINT:
            case DXGI_FORMAT_R8G8_SINT:
            case DXGI_FORMAT_R8G8_UINT:
            case DXGI_FORMAT_R8G8B8A8_SINT:
            case DXGI_FORMAT_R8G8B8A8_UINT:
            case DXGI_FORMAT_R16_SINT:
            case DXGI_FORMAT_R16_UINT:
            case DXGI_FORMAT_R16G16_SINT:
            case DXGI_FORMAT_R16G16_UINT:
            case DXGI_FORMAT_R16G16B16A16_SINT:
            case DXGI_FORMAT_R16G16B16A16_UINT:
            case DXGI_FORMAT_R32_SINT:
            case DXGI_FORMAT_R32_UINT:
            case DXGI_FORMAT_R32G32_SINT:
            case DXGI_FORMAT_R32G32_UINT:
            case DXGI_FORMAT_R32G32B32_SINT:
            case DXGI_FORMAT_R32G32B32_UINT:
            case DXGI_FORMAT_R32G32B32A32_SINT:
            case DXGI_FORMAT_R32G32B32A32_UINT:
            case DXGI_FORMAT_R10G10B10A2_UINT:
                return true;
            default:
                return false;
            }
        }

        std::string DrainInfoQueue(ID3D12Device *device)
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
            if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
                return {};

            const UINT64 count = infoQueue->GetNumStoredMessages();
            if (count == 0)
                return {};

            std::ostringstream out;
            const UINT64 first = count > 8 ? count - 8 : 0;
            for (UINT64 i = first; i < count; ++i)
            {
                SIZE_T messageLength = 0;
                if (FAILED(infoQueue->GetMessage(i, nullptr, &messageLength)) || messageLength == 0)
                    continue;

                std::vector<char> storage(messageLength);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (FAILED(infoQueue->GetMessage(i, message, &messageLength)) || !message->pDescription)
                    continue;

                if (out.tellp() > 0)
                    out << " | ";
                out << message->pDescription;
            }
            return out.str();
        }

        D3D12_BLEND_DESC BlitBlendState()
        {
            D3D12_RENDER_TARGET_BLEND_DESC attachment{};
            attachment.BlendEnable = FALSE;
            attachment.LogicOpEnable = FALSE;
            attachment.SrcBlend = D3D12_BLEND_ONE;
            attachment.DestBlend = D3D12_BLEND_ZERO;
            attachment.BlendOp = D3D12_BLEND_OP_ADD;
            attachment.SrcBlendAlpha = D3D12_BLEND_ONE;
            attachment.DestBlendAlpha = D3D12_BLEND_ZERO;
            attachment.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            attachment.LogicOp = D3D12_LOGIC_OP_NOOP;
            attachment.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

            D3D12_BLEND_DESC desc{};
            desc.AlphaToCoverageEnable = FALSE;
            desc.IndependentBlendEnable = FALSE;
            for (D3D12_RENDER_TARGET_BLEND_DESC &target : desc.RenderTarget)
                target = attachment;
            return desc;
        }

        D3D12_RASTERIZER_DESC BlitRasterizerState()
        {
            D3D12_RASTERIZER_DESC desc{};
            desc.FillMode = D3D12_FILL_MODE_SOLID;
            desc.CullMode = D3D12_CULL_MODE_NONE;
            desc.FrontCounterClockwise = FALSE;
            desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            desc.DepthClipEnable = TRUE;
            desc.MultisampleEnable = FALSE;
            desc.AntialiasedLineEnable = FALSE;
            desc.ForcedSampleCount = 0;
            desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            return desc;
        }

        D3D12_DEPTH_STENCIL_DESC BlitDepthStencilState()
        {
            D3D12_DEPTH_STENCIL_DESC desc{};
            desc.DepthEnable = FALSE;
            desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.StencilEnable = FALSE;
            desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.BackFace = desc.FrontFace;
            return desc;
        }

        ID3D12PipelineState *GetShaderBlitPso(DXGI_FORMAT colorFormat)
        {
            static std::mutex mutex;
            static ID3D12Device *cachedDevice = nullptr;
            static std::unordered_map<DXGI_FORMAT, Microsoft::WRL::ComPtr<ID3D12PipelineState>> psos;

            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            PE_ERROR_IF(!rhi || !rhi->GetDevice() || !rhi->GetSharedRootSig(),
                        "Dx12BlitImage: DX12 device/root signature unavailable");

            ID3D12Device *device = rhi->GetDevice();
            std::lock_guard<std::mutex> guard(mutex);
            if (cachedDevice != device)
            {
                psos.clear();
                cachedDevice = device;
            }

            auto it = psos.find(colorFormat);
            if (it != psos.end())
                return it->second.Get();

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rhi->GetSharedRootSig()->Get();
            desc.VS = {kDx12BlitFullscreenTriangleVS, sizeof(kDx12BlitFullscreenTriangleVS)};
            desc.PS = {kDx12BlitSamplePS, sizeof(kDx12BlitSamplePS)};
            desc.BlendState = BlitBlendState();
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = BlitRasterizerState();
            desc.DepthStencilState = BlitDepthStencilState();
            desc.InputLayout = {nullptr, 0};
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = colorFormat;
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.NodeMask = 0;
            desc.CachedPSO = {};
            desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

            Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
            const HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
            const std::string infoQueueMessages = FAILED(hr) ? DrainInfoQueue(device) : std::string{};
            PE_ERROR_IF(FAILED(hr),
                        "Dx12BlitImage: CreateGraphicsPipelineState failed (0x%08X, format=%u)%s%s",
                        static_cast<unsigned>(hr),
                        static_cast<uint32_t>(colorFormat),
                        infoQueueMessages.empty() ? "" : ": ",
                        infoQueueMessages.c_str());

            ID3D12PipelineState *result = pso.Get();
            psos.emplace(colorFormat, std::move(pso));
            return result;
        }

        void ValidateShaderBlitFormat(ID3D12Device *device, DXGI_FORMAT format)
        {
            PE_ERROR_IF(IsIntegerFormat(format),
                        "Dx12BlitImage: scaled integer-format blits need a typed shader variant (format=%u)",
                        static_cast<uint32_t>(format));

            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = format;
            const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            PE_ERROR_IF(FAILED(hr),
                        "Dx12BlitImage: CheckFeatureSupport failed for format %u (0x%08X)",
                        static_cast<uint32_t>(format),
                        static_cast<unsigned>(hr));
            PE_ERROR_IF((support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == 0 ||
                            (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0,
                        "Dx12BlitImage: format %u is not both sampleable and render-target capable",
                        static_cast<uint32_t>(format));
        }

        Descriptor *CreateShaderBlitDescriptor(ImageView *srcView, Sampler *sampler)
        {
            std::vector<DescriptorBindingInfo> bindingInfos(1);
            bindingInfos[0].binding = 0;
            bindingInfos[0].dxRegister = 0;
            bindingInfos[0].dxSpace = 0;
            bindingInfos[0].type = PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindingInfos[0].imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bindingInfos[0].name = "Source";

            Descriptor *descriptor = Descriptor::Create(bindingInfos, PE_SHADER_STAGE_FRAGMENT, false, "Dx12BlitImageDescriptor");
            descriptor->SetImageView(0, srcView, sampler);
            descriptor->Update();
            return descriptor;
        }

        void CopyTextureRegionBlit(CommandBuffer *cmd, Image *src, Image *dst, const ImageBlit &region, const BlitGeometry &geometry)
        {
            const Dx12ImageImpl *srcImpl = Dx12ImageImpl::From(src);
            const Dx12ImageImpl *dstImpl = Dx12ImageImpl::From(dst);
            ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);
            PE_ERROR_IF(!list, "Dx12BlitImage: command list unavailable");

            D3D12_BOX srcBox{};
            srcBox.left = static_cast<UINT>(geometry.srcMin.x);
            srcBox.top = static_cast<UINT>(geometry.srcMin.y);
            srcBox.front = static_cast<UINT>(geometry.srcMin.z);
            srcBox.right = static_cast<UINT>(geometry.srcMax.x);
            srcBox.bottom = static_cast<UINT>(geometry.srcMax.y);
            srcBox.back = static_cast<UINT>(geometry.srcBack);

            const uint32_t layerCount = region.srcSubresource.layerCount ? region.srcSubresource.layerCount : 1u;
            for (uint32_t i = 0; i < layerCount; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = srcImpl->GetResource();
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = SubresourceIndex(src,
                                                           region.srcSubresource.baseArrayLayer + i,
                                                           region.srcSubresource.mipLevel);

                D3D12_TEXTURE_COPY_LOCATION dest{};
                dest.pResource = dstImpl->GetResource();
                dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dest.SubresourceIndex = SubresourceIndex(dst,
                                                         region.dstSubresource.baseArrayLayer + i,
                                                         region.dstSubresource.mipLevel);

                list->CopyTextureRegion(&dest,
                                        static_cast<UINT>(geometry.dstMin.x),
                                        static_cast<UINT>(geometry.dstMin.y),
                                        static_cast<UINT>(geometry.dstMin.z),
                                        &source,
                                        &srcBox);
            }
        }

        void ShaderBlit(CommandBuffer *cmd, Image *src, Image *dst, const ImageBlit &region, PeFilter filter, const BlitGeometry &geometry)
        {
            Dx12CommandBufferImpl *dxCmd = Dx12CommandBufferImpl::From(cmd);
            const Dx12ImageImpl *srcImpl = Dx12ImageImpl::From(src);
            const Dx12ImageImpl *dstImpl = Dx12ImageImpl::From(dst);

            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12BlitImage: DX12 device unavailable");

            const D3D12_RESOURCE_DESC srcDesc = srcImpl->GetResource()->GetDesc();
            const D3D12_RESOURCE_DESC dstDesc = dstImpl->GetResource()->GetDesc();
            PE_ERROR_IF(srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                            dstDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                            srcDesc.DepthOrArraySize != 1 ||
                            dstDesc.DepthOrArraySize != 1 ||
                            geometry.srcD != 1 ||
                            geometry.dstD != 1,
                        "Dx12BlitImage: scaled shader blits only support non-array 2D images");
            PE_ERROR_IF(region.srcSubresource.baseArrayLayer != 0 ||
                            region.dstSubresource.baseArrayLayer != 0,
                        "Dx12BlitImage: scaled shader blits only support base array layer 0");
            PE_ERROR_IF((src->GetUsage() & PE_IMAGE_USAGE_SAMPLED) == 0,
                        "Dx12BlitImage: source image '%s' was not created with SAMPLED usage",
                        src->GetName().c_str());
            PE_ERROR_IF((dst->GetUsage() & PE_IMAGE_USAGE_COLOR_ATTACHMENT) == 0,
                        "Dx12BlitImage: destination image '%s' was not created with COLOR_ATTACHMENT usage",
                        dst->GetName().c_str());

            ValidateShaderBlitFormat(rhi->GetDevice(), dstImpl->GetResourceFormat());

            ImageViewDesc srcViewDesc{};
            srcViewDesc.viewType = PE_IMAGE_VIEW_TYPE_2D;
            srcViewDesc.format = src->GetFormat();
            srcViewDesc.aspectMask = PE_IMAGE_ASPECT_COLOR;
            srcViewDesc.baseMipLevel = region.srcSubresource.mipLevel;
            srcViewDesc.levelCount = 1;
            srcViewDesc.baseArrayLayer = 0;
            srcViewDesc.layerCount = 1;

            ImageViewDesc dstViewDesc{};
            dstViewDesc.viewType = PE_IMAGE_VIEW_TYPE_2D;
            dstViewDesc.format = dst->GetFormat();
            dstViewDesc.aspectMask = PE_IMAGE_ASPECT_COLOR;
            dstViewDesc.baseMipLevel = region.dstSubresource.mipLevel;
            dstViewDesc.levelCount = 1;
            dstViewDesc.baseArrayLayer = 0;
            dstViewDesc.layerCount = 1;

            ImageView *srcView = Dx12ImageViewImpl::Create(src, srcViewDesc, Dx12ImageViewKind::Srv, "Dx12BlitImageSrcView");
            ImageView *dstView = Dx12ImageViewImpl::Create(dst, dstViewDesc, Dx12ImageViewKind::Rtv, "Dx12BlitImageDstView");

            SamplerDesc samplerDesc = Sampler::CreateInfoInit();
            samplerDesc.magFilter = filter;
            samplerDesc.minFilter = filter;
            samplerDesc.mipmapMode = PE_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerDesc.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerDesc.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerDesc.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerDesc.anisotropyEnable = false;
            samplerDesc.maxAnisotropy = 1.0f;

            Sampler *sampler = Sampler::Create(samplerDesc, "Dx12BlitImageSampler");
            Descriptor *descriptor = CreateShaderBlitDescriptor(srcView, sampler);
            cmd->AddAfterWaitCallback([srcView, dstView, sampler, descriptor]() mutable
                                      {
                                          Descriptor::Destroy(descriptor);
                                          Sampler::Destroy(sampler);
                                          ImageView::Destroy(dstView);
                                          ImageView::Destroy(srcView); });

            ImageBarrierInfo srcBarrier{};
            srcBarrier.image = src;
            srcBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            srcBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            srcBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            srcBarrier.baseArrayLayer = region.srcSubresource.baseArrayLayer;
            srcBarrier.arrayLayers = 1;
            srcBarrier.baseMipLevel = region.srcSubresource.mipLevel;
            srcBarrier.mipLevels = 1;
            cmd->ImageBarrier(srcBarrier);
            dxCmd->FlushBarriers();

            Attachment attachment{};
            attachment.image = dst;
            attachment.view = dstView;
            attachment.loadOp = PE_LOAD_OP_LOAD;
            attachment.storeOp = PE_STORE_OP_STORE;

            dxCmd->BeginPass(1, &attachment, "Dx12BlitImageScale", false);
            dxCmd->BindExternalRenderPipeline(GetShaderBlitPso(dstImpl->GetResourceFormat()),
                                              D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                                              {});
            dxCmd->BindExternalRenderDescriptors(1, &descriptor);

            const uint32_t srcExtentW = MipExtent(src->GetWidth(), region.srcSubresource.mipLevel);
            const uint32_t srcExtentH = MipExtent(src->GetHeight(), region.srcSubresource.mipLevel);
            const float constants[4] = {
                static_cast<float>(geometry.srcW) / static_cast<float>(srcExtentW),
                -static_cast<float>(geometry.srcH) / static_cast<float>(srcExtentH),
                static_cast<float>(geometry.srcMin.x) / static_cast<float>(srcExtentW),
                static_cast<float>(geometry.srcMin.y + geometry.srcH) / static_cast<float>(srcExtentH),
            };
            dxCmd->Get()->SetGraphicsRoot32BitConstants(DX12_ROOT_CONSTANTS_INDEX, 4, constants, 0);

            dxCmd->SetViewport(static_cast<float>(geometry.dstMin.x),
                               static_cast<float>(geometry.dstMin.y),
                               static_cast<float>(geometry.dstW),
                               static_cast<float>(geometry.dstH),
                               0.0f,
                               1.0f);
            dxCmd->SetScissor(geometry.dstMin.x,
                              geometry.dstMin.y,
                              static_cast<uint32_t>(geometry.dstW),
                              static_cast<uint32_t>(geometry.dstH));
            dxCmd->Draw(3, 1, 0, 0);
            dxCmd->EndPass();
        }
    } // namespace

    void Dx12BlitImage(CommandBuffer *cmd, Image *src, Image *dst, const ImageBlit &region, PeFilter filter)
    {
        PE_ERROR_IF(!cmd, "Dx12BlitImage: null command buffer");
        PE_ERROR_IF(!src, "Dx12BlitImage: null source image");
        PE_ERROR_IF(!dst, "Dx12BlitImage: null destination image");

        const Dx12ImageImpl *srcImpl = Dx12ImageImpl::From(src);
        const Dx12ImageImpl *dstImpl = Dx12ImageImpl::From(dst);
        PE_ERROR_IF(srcImpl->GetResourceFormat() != dstImpl->GetResourceFormat(),
                    "Dx12BlitImage: format-converting blit is not supported yet");

        const BlitGeometry geometry = CalculateBlitGeometry(region);
        if (!IsScalingBlit(geometry))
        {
            CopyTextureRegionBlit(cmd, src, dst, region, geometry);
            return;
        }

        PE_ERROR_IF(src == dst && OverlapsSubresource(region),
                    "Dx12BlitImage: in-place scaled blit is not supported");
        ShaderBlit(cmd, src, dst, region, filter, geometry);
    }
} // namespace pe

#endif // PE_WIN32
