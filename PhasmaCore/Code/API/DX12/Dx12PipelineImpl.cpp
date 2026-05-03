#include "API/DX12/Dx12PipelineImpl.h"

#if defined(PE_WIN32)

#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12ShaderImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"
#include "API/Reflection.h"

namespace pe
{
    namespace
    {
        D3D12_RASTERIZER_DESC MakeRasterizerDesc(const PassInfo &info)
        {
            D3D12_RASTERIZER_DESC desc{};
            desc.FillMode = pe_dx12::FillMode(info.polygonMode);
            desc.CullMode = pe_dx12::CullMode(info.cullMode);
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

        D3D12_BLEND_DESC MakeBlendDesc(const PassInfo &info)
        {
            D3D12_BLEND_DESC desc{};
            desc.AlphaToCoverageEnable = FALSE;
            desc.IndependentBlendEnable = TRUE;

            const uint32_t count = std::min<uint32_t>(
                static_cast<uint32_t>(info.colorBlendAttachments.size()),
                D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
            for (uint32_t i = 0; i < count; ++i)
            {
                BlendState attachment = info.colorBlendAttachments[i];
                attachment.blendEnable = attachment.blendEnable && info.blendEnable;
                desc.RenderTarget[i] = pe_dx12::BlendAttachment(attachment);
            }
            for (uint32_t i = count; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            {
                desc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            return desc;
        }

        D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(const PassInfo &info)
        {
            D3D12_DEPTH_STENCIL_DESC desc{};
            desc.DepthEnable = info.depthTestEnable ? TRUE : FALSE;
            desc.DepthWriteMask = info.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = pe_dx12::CompareOp(info.depthCompareOp);
            desc.StencilEnable = info.stencilTestEnable ? TRUE : FALSE;
            desc.StencilReadMask = static_cast<UINT8>(info.stencilCompareMask);
            desc.StencilWriteMask = static_cast<UINT8>(info.stencilWriteMask);
            desc.FrontFace.StencilFailOp = pe_dx12::StencilOp(info.stencilFailOp);
            desc.FrontFace.StencilDepthFailOp = pe_dx12::StencilOp(info.stencilDepthFailOp);
            desc.FrontFace.StencilPassOp = pe_dx12::StencilOp(info.stencilPassOp);
            desc.FrontFace.StencilFunc = pe_dx12::CompareOp(info.stencilCompareOp);
            desc.BackFace = desc.FrontFace;
            return desc;
        }

    } // namespace

    Dx12PipelineImpl::Dx12PipelineImpl(Pipeline *owner, RenderPass * /*renderPass*/, PassInfo &info)
        : m_owner{owner}, m_info(info)
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12PipelineImpl requires an initialized DX12 device");
        PE_ERROR_IF(!rhi->GetSharedRootSig(), "Dx12PipelineImpl requires the shared DX12 root signature");
        PE_ERROR_IF(info.acceleration.rayGen, "Dx12PipelineImpl: ray tracing pipelines are disabled for Phase 1");

        if (info.pCompShader)
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rhi->GetSharedRootSig()->Get();
            desc.CS = GetDx12ShaderBytecode(info.pCompShader);

            HRESULT hr = rhi->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_pso));
            PE_ERROR_IF(FAILED(hr), "Dx12PipelineImpl: CreateComputePipelineState failed (0x%08X)", static_cast<unsigned>(hr));
            return;
        }

        info.m_pushConstantStages.clear();
        info.m_pushConstantOffsets.clear();
        info.m_pushConstantSizes.clear();

        const PushConstantDesc &pushConstantVert = info.pVertShader ? info.pVertShader->GetPushConstantDesc() : PushConstantDesc{};
        const PushConstantDesc &pushConstantFrag = info.pFragShader ? info.pFragShader->GetPushConstantDesc() : PushConstantDesc{};
        const uint32_t pushConstantSize = static_cast<uint32_t>(std::max(pushConstantVert.size, pushConstantFrag.size));
        if (pushConstantSize > 0)
        {
            PE_ERROR_IF(pushConstantSize > RHII.GetMaxPushConstantsSize(), "DX12 push constant size is greater than maxPushConstantsSize");
            info.m_pushConstantStages.push_back(PE_SHADER_STAGE_VERTEX | PE_SHADER_STAGE_FRAGMENT);
            info.m_pushConstantOffsets.push_back(0);
            info.m_pushConstantSizes.push_back(pushConstantSize);
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
        std::vector<std::string> semanticNames;
        if (info.pVertShader)
        {
            std::unordered_map<uint32_t, uint32_t> bindingOffsets;
            const auto &inputs = info.pVertShader->GetReflection().GetInputs();
            semanticNames.reserve(inputs.size());
            inputElements.reserve(inputs.size());
            for (const ShaderInOutDesc &input : inputs)
            {
                if (input.binding == INT32_MIN)
                    continue;

                uint32_t &offset = bindingOffsets.try_emplace(static_cast<uint32_t>(input.binding), 0).first->second;
                semanticNames.push_back(input.name);
                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = input.semanticIndex;
                element.Format = pe_dx12::Format(input.format);
                element.InputSlot = static_cast<UINT>(input.binding);
                element.AlignedByteOffset = offset;
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                element.InstanceDataStepRate = 0;
                inputElements.push_back(element);
                offset += input.size;
            }
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rhi->GetSharedRootSig()->Get();
        desc.VS = GetDx12ShaderBytecode(info.pVertShader);
        desc.PS = GetDx12ShaderBytecode(info.pFragShader);
        desc.BlendState = MakeBlendDesc(info);
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState = MakeRasterizerDesc(info);
        desc.DepthStencilState = MakeDepthStencilDesc(info);
        desc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
        desc.PrimitiveTopologyType = pe_dx12::TopologyType(info.topology);
        desc.NumRenderTargets = std::min<UINT>(static_cast<UINT>(info.colorFormats.size()), D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
        for (UINT i = 0; i < desc.NumRenderTargets; ++i)
            desc.RTVFormats[i] = pe_dx12::Format(info.colorFormats[i]);
        desc.DSVFormat = pe_dx12::Format(info.depthFormat);
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.NodeMask = 0;
        desc.CachedPSO = {};
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        HRESULT hr = rhi->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pso));
        PE_ERROR_IF(FAILED(hr), "Dx12PipelineImpl: CreateGraphicsPipelineState failed (0x%08X)", static_cast<unsigned>(hr));
    }
} // namespace pe

#endif // PE_WIN32
