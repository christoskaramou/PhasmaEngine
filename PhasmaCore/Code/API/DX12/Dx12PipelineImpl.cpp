#include "API/DX12/Dx12PipelineImpl.h"

#if defined(PE_WIN32)

#include "API/Buffer.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12ShaderImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"
#include "API/Reflection.h"

#include <algorithm>
#include <cstring>
#include <deque>

namespace pe
{
    namespace
    {
        constexpr uint32_t kDx12RayPayloadSizeBytes = 64;
        constexpr uint32_t kDx12RayAttributeSizeBytes = 8;

        uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
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
            desc.IndependentBlendEnable = info.colorBlendAttachments.size() > 1 ? TRUE : FALSE;

            const D3D12_RENDER_TARGET_BLEND_DESC defaultAttachment =
                pe_dx12::BlendAttachment(PeBlendAttachmentState{});
            for (uint32_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
                desc.RenderTarget[i] = defaultAttachment;

            const uint32_t count = std::min<uint32_t>(
                static_cast<uint32_t>(info.colorBlendAttachments.size()),
                D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
            for (uint32_t i = 0; i < count; ++i)
            {
                BlendState attachment = info.colorBlendAttachments[i];
                attachment.blendEnable = attachment.blendEnable && info.blendEnable;
                desc.RenderTarget[i] = pe_dx12::BlendAttachment(attachment);
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

        std::wstring ToWide(const std::string &str)
        {
            if (str.empty())
                return {};

            int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
            if (sizeNeeded <= 0)
                return {};

            std::wstring wide(static_cast<size_t>(sizeNeeded), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide.data(), sizeNeeded);
            wide.resize(static_cast<size_t>(sizeNeeded - 1));
            return wide;
        }

        uint32_t GetMaxRayTracingPushConstantSize(const Acceleration &acceleration)
        {
            uint32_t size = 0;
            if (acceleration.rayGen)
                size = std::max<uint32_t>(size, static_cast<uint32_t>(acceleration.rayGen->GetPushConstantDesc().size));
            for (Shader *shader : acceleration.miss)
            {
                if (shader)
                    size = std::max<uint32_t>(size, static_cast<uint32_t>(shader->GetPushConstantDesc().size));
            }
            for (const HitGroup &hitGroup : acceleration.hitGroups)
            {
                if (hitGroup.closestHit)
                    size = std::max<uint32_t>(size, static_cast<uint32_t>(hitGroup.closestHit->GetPushConstantDesc().size));
                if (hitGroup.anyHit)
                    size = std::max<uint32_t>(size, static_cast<uint32_t>(hitGroup.anyHit->GetPushConstantDesc().size));
                if (hitGroup.intersection)
                    size = std::max<uint32_t>(size, static_cast<uint32_t>(hitGroup.intersection->GetPushConstantDesc().size));
            }
            return size;
        }

        Microsoft::WRL::ComPtr<ID3D12Device5> GetDx12Device5(ID3D12Device *device)
        {
            Microsoft::WRL::ComPtr<ID3D12Device5> device5;
            PE_ERROR_IF(!device, "Dx12PipelineImpl: DX12 device unavailable");
            const HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device5));
            PE_ERROR_IF(FAILED(hr), "Dx12PipelineImpl: ID3D12Device5 unavailable for DXR pipeline creation (0x%08X)", static_cast<unsigned>(hr));
            return device5;
        }

        bool IsRayTracingPipelineInfo(const PassInfo &info)
        {
            return info.acceleration.rayGen != nullptr;
        }

    } // namespace

    Dx12PipelineImpl::~Dx12PipelineImpl()
    {
        Buffer::Destroy(m_sbtBuffer);
    }

    Dx12PipelineImpl::Dx12PipelineImpl(Pipeline *owner, RenderPass * /*renderPass*/, PassInfo &info)
        : m_owner{owner}, m_info(info)
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12PipelineImpl requires an initialized DX12 device");
        PE_ERROR_IF(!rhi->GetSharedRootSig(), "Dx12PipelineImpl requires the shared DX12 root signature");

        info.m_pushConstantStages.clear();
        info.m_pushConstantOffsets.clear();
        info.m_pushConstantSizes.clear();

        if (IsRayTracingPipelineInfo(info))
        {
            CreateRayTracingPipeline();
            return;
        }

        if (info.pCompShader)
        {
            const PushConstantDesc &pushConstantComp = info.pCompShader->GetPushConstantDesc();
            if (pushConstantComp.size > 0)
            {
                PE_ERROR_IF(pushConstantComp.size > RHII.GetMaxPushConstantsSize(), "DX12 compute push constant size is greater than maxPushConstantsSize");
                info.m_pushConstantStages.push_back(PE_SHADER_STAGE_COMPUTE);
                info.m_pushConstantOffsets.push_back(0);
                info.m_pushConstantSizes.push_back(static_cast<uint32_t>(pushConstantComp.size));
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = rhi->GetSharedRootSig()->Get();
            desc.CS = GetDx12ShaderBytecode(info.pCompShader);

            HRESULT hr = rhi->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_pso));
            PE_ERROR_IF(FAILED(hr), "Dx12PipelineImpl: CreateComputePipelineState failed (0x%08X)", static_cast<unsigned>(hr));
            return;
        }

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

            for (const auto &[binding, stride] : bindingOffsets)
            {
                if (binding >= m_vertexBindingStrides.size())
                    m_vertexBindingStrides.resize(binding + 1, 0u);
                m_vertexBindingStrides[binding] = stride;
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
        desc.InputLayout = {inputElements.empty() ? nullptr : inputElements.data(), static_cast<UINT>(inputElements.size())};
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
        const std::string infoQueueMessages = FAILED(hr) ? DrainInfoQueue(rhi->GetDevice()) : std::string{};
        PE_ERROR_IF(FAILED(hr),
                    "Dx12PipelineImpl('%s'): CreateGraphicsPipelineState failed (0x%08X, colorTargets=%u, depthFormat=%d, depthTest=%d, depthWrite=%d)%s%s",
                    info.name.c_str(),
                    static_cast<unsigned>(hr),
                    desc.NumRenderTargets,
                    static_cast<int>(info.depthFormat),
                    info.depthTestEnable ? 1 : 0,
                    info.depthWriteEnable ? 1 : 0,
                    infoQueueMessages.empty() ? "" : ": ",
                    infoQueueMessages.c_str());
    }

    void Dx12PipelineImpl::CreateRayTracingPipeline()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12PipelineImpl requires an initialized DX12 device");
        PE_ERROR_IF(!rhi->SupportsDxr(), "Dx12PipelineImpl: DXR is not supported on this device");
        PE_ERROR_IF(!m_info.acceleration.rayGen, "Dx12PipelineImpl: ray tracing pipeline requires a raygen shader");

        Microsoft::WRL::ComPtr<ID3D12Device5> device5 = GetDx12Device5(rhi->GetDevice());

        const uint32_t hitGroupCount = static_cast<uint32_t>(m_info.acceleration.hitGroups.size());
        const uint32_t missCount = static_cast<uint32_t>(m_info.acceleration.miss.size());
        const uint32_t shaderCount = 1u + missCount + hitGroupCount * 3u;

        std::deque<std::wstring> names;
        auto keepName = [&](const std::string &name) -> LPCWSTR
        {
            names.emplace_back(ToWide(name));
            return names.back().c_str();
        };

        std::vector<std::vector<D3D12_EXPORT_DESC>> exportLists;
        std::vector<D3D12_DXIL_LIBRARY_DESC> libraries;
        std::vector<D3D12_HIT_GROUP_DESC> hitGroups;
        std::vector<D3D12_STATE_SUBOBJECT> subobjects;
        std::vector<std::wstring> missExports;
        std::vector<std::wstring> hitGroupExports;
        std::vector<LPCWSTR> shaderConfigExportPtrs;

        exportLists.reserve(shaderCount);
        libraries.reserve(shaderCount);
        hitGroups.reserve(hitGroupCount);
        subobjects.reserve(shaderCount + hitGroupCount + 4);
        missExports.reserve(missCount);
        hitGroupExports.reserve(hitGroupCount);
        shaderConfigExportPtrs.reserve(1u + missCount + hitGroupCount);

        auto addLibrary = [&](Shader *shader, const std::string &exportName)
        {
            PE_ERROR_IF(!shader, "Dx12PipelineImpl: null DXR shader for export '%s'", exportName.c_str());
            PE_ERROR_IF(!GetDx12ShaderDxil(shader) || GetDx12ShaderDxilSizeBytes(shader) == 0,
                        "Dx12PipelineImpl: DXR shader '%s' has no DXIL bytecode",
                        shader->GetEntryName().c_str());

            std::vector<D3D12_EXPORT_DESC> exports;
            exports.resize(1);
            exports[0].Name = keepName(exportName);
            exports[0].ExportToRename = keepName(shader->GetEntryName());
            exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
            exportLists.push_back(std::move(exports));

            D3D12_DXIL_LIBRARY_DESC lib{};
            lib.DXILLibrary.pShaderBytecode = GetDx12ShaderDxil(shader);
            lib.DXILLibrary.BytecodeLength = GetDx12ShaderDxilSizeBytes(shader);
            lib.NumExports = static_cast<UINT>(exportLists.back().size());
            lib.pExports = exportLists.back().data();
            libraries.push_back(lib);

            D3D12_STATE_SUBOBJECT subobject{};
            subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            subobject.pDesc = &libraries.back();
            subobjects.push_back(subobject);
        };

        const std::string rayGenExportName = "RayGen0";
        std::wstring rayGenExport = ToWide(rayGenExportName);
        addLibrary(m_info.acceleration.rayGen, rayGenExportName);
        shaderConfigExportPtrs.push_back(keepName(rayGenExportName));

        for (uint32_t i = 0; i < missCount; ++i)
        {
            const std::string exportName = "Miss" + std::to_string(i);
            addLibrary(m_info.acceleration.miss[i], exportName);
            missExports.push_back(ToWide(exportName));
            shaderConfigExportPtrs.push_back(keepName(exportName));
        }

        for (uint32_t i = 0; i < hitGroupCount; ++i)
        {
            const HitGroup &source = m_info.acceleration.hitGroups[i];
            std::string closestExportName;
            std::string anyHitExportName;
            std::string intersectionExportName;

            PE_ERROR_IF(!source.closestHit && !source.anyHit && !source.intersection,
                        "Dx12PipelineImpl: hit group %u has no shaders",
                        i);

            if (source.closestHit)
            {
                closestExportName = "ClosestHit" + std::to_string(i);
                addLibrary(source.closestHit, closestExportName);
            }
            if (source.anyHit)
            {
                anyHitExportName = "AnyHit" + std::to_string(i);
                addLibrary(source.anyHit, anyHitExportName);
            }
            if (source.intersection)
            {
                intersectionExportName = "Intersection" + std::to_string(i);
                addLibrary(source.intersection, intersectionExportName);
            }

            const std::string groupName = "HitGroup" + std::to_string(i);
            hitGroupExports.push_back(ToWide(groupName));

            D3D12_HIT_GROUP_DESC hitGroup{};
            hitGroup.Type = source.intersection ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE : D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hitGroup.HitGroupExport = keepName(groupName);
            hitGroup.ClosestHitShaderImport = closestExportName.empty() ? nullptr : keepName(closestExportName);
            hitGroup.AnyHitShaderImport = anyHitExportName.empty() ? nullptr : keepName(anyHitExportName);
            hitGroup.IntersectionShaderImport = intersectionExportName.empty() ? nullptr : keepName(intersectionExportName);
            hitGroups.push_back(hitGroup);

            D3D12_STATE_SUBOBJECT subobject{};
            subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            subobject.pDesc = &hitGroups.back();
            subobjects.push_back(subobject);

            shaderConfigExportPtrs.push_back(keepName(groupName));
        }

        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = kDx12RayPayloadSizeBytes;
        shaderConfig.MaxAttributeSizeInBytes = kDx12RayAttributeSizeBytes;
        D3D12_STATE_SUBOBJECT shaderConfigSubobject{};
        shaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigSubobject.pDesc = &shaderConfig;
        subobjects.push_back(shaderConfigSubobject);
        const D3D12_STATE_SUBOBJECT *shaderConfigRef = &subobjects.back();

        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation{};
        shaderConfigAssociation.pSubobjectToAssociate = shaderConfigRef;
        shaderConfigAssociation.NumExports = static_cast<UINT>(shaderConfigExportPtrs.size());
        shaderConfigAssociation.pExports = shaderConfigExportPtrs.data();
        D3D12_STATE_SUBOBJECT shaderConfigAssociationSubobject{};
        shaderConfigAssociationSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
        shaderConfigAssociationSubobject.pDesc = &shaderConfigAssociation;
        subobjects.push_back(shaderConfigAssociationSubobject);

        D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature{};
        globalRootSignature.pGlobalRootSignature = rhi->GetSharedRootSig()->Get();
        D3D12_STATE_SUBOBJECT globalRootSignatureSubobject{};
        globalRootSignatureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        globalRootSignatureSubobject.pDesc = &globalRootSignature;
        subobjects.push_back(globalRootSignatureSubobject);

        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
        pipelineConfig.MaxTraceRecursionDepth = m_info.acceleration.maxRecursionDepth;
        D3D12_STATE_SUBOBJECT pipelineConfigSubobject{};
        pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        pipelineConfigSubobject.pDesc = &pipelineConfig;
        subobjects.push_back(pipelineConfigSubobject);

        D3D12_STATE_OBJECT_DESC desc{};
        desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        desc.NumSubobjects = static_cast<UINT>(subobjects.size());
        desc.pSubobjects = subobjects.data();

        HRESULT hr = device5->CreateStateObject(&desc, IID_PPV_ARGS(&m_rtStateObject));
        const std::string infoQueueMessages = FAILED(hr) ? DrainInfoQueue(rhi->GetDevice()) : std::string{};
        PE_ERROR_IF(FAILED(hr),
                    "Dx12PipelineImpl('%s'): CreateStateObject failed (0x%08X)%s%s",
                    m_info.name.c_str(),
                    static_cast<unsigned>(hr),
                    infoQueueMessages.empty() ? "" : ": ",
                    infoQueueMessages.c_str());
        PE_CHECK(m_rtStateObject.As(&m_rtProperties));

        const uint32_t pushConstantSize = GetMaxRayTracingPushConstantSize(m_info.acceleration);
        if (pushConstantSize > 0)
        {
            PE_ERROR_IF(pushConstantSize > RHII.GetMaxPushConstantsSize(), "DX12 ray tracing push constant size is greater than maxPushConstantsSize");
            m_info.m_pushConstantStages.push_back(PE_SHADER_STAGE_RAYGEN_KHR | PE_SHADER_STAGE_MISS_KHR | PE_SHADER_STAGE_CLOSEST_HIT_KHR | PE_SHADER_STAGE_ANY_HIT_KHR);
            m_info.m_pushConstantOffsets.push_back(0);
            m_info.m_pushConstantSizes.push_back(pushConstantSize);
        }

        CreateRayTracingSBT(rayGenExport, missExports, hitGroupExports);
    }

    void Dx12PipelineImpl::CreateRayTracingSBT(const std::wstring &rayGenExport,
                                               const std::vector<std::wstring> &missExports,
                                               const std::vector<std::wstring> &hitGroupExports)
    {
        PE_ERROR_IF(!m_rtProperties, "Dx12PipelineImpl::CreateRayTracingSBT requires state-object properties");

        const uint32_t recordSize = static_cast<uint32_t>(AlignUp(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                                                                  D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));
        const uint64_t rayGenSize = AlignUp(recordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const uint64_t missSize = AlignUp(static_cast<uint64_t>(missExports.size()) * recordSize,
                                          D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const uint64_t hitSize = AlignUp(static_cast<uint64_t>(hitGroupExports.size()) * recordSize,
                                         D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const uint64_t totalSize = rayGenSize + missSize + hitSize;
        PE_ERROR_IF(totalSize == 0, "Dx12PipelineImpl::CreateRayTracingSBT produced an empty table");

        m_sbtBuffer = Buffer::Create({
            .size = static_cast<size_t>(totalSize),
            .usage = PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = m_info.name + "_SBT",
        });

        m_sbtBuffer->Map();
        auto *data = static_cast<uint8_t *>(m_sbtBuffer->Data());
        std::memset(data, 0, static_cast<size_t>(totalSize));

        auto copyIdentifier = [&](uint8_t *dst, const std::wstring &exportName)
        {
            void *identifier = m_rtProperties->GetShaderIdentifier(exportName.c_str());
            PE_ERROR_IF(!identifier, "Dx12PipelineImpl: shader identifier '%ls' not found", exportName.c_str());
            std::memcpy(dst, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        };

        copyIdentifier(data, rayGenExport);

        uint8_t *missData = data + rayGenSize;
        for (size_t i = 0; i < missExports.size(); ++i)
            copyIdentifier(missData + i * recordSize, missExports[i]);

        uint8_t *hitData = missData + missSize;
        for (size_t i = 0; i < hitGroupExports.size(); ++i)
            copyIdentifier(hitData + i * recordSize, hitGroupExports[i]);

        m_sbtBuffer->Flush();
        m_sbtBuffer->Unmap();

        const D3D12_GPU_VIRTUAL_ADDRESS base = m_sbtBuffer->GetDeviceAddress();
        m_rayGenRegion.StartAddress = base;
        m_rayGenRegion.SizeInBytes = recordSize;
        m_missRegion.StartAddress = base + rayGenSize;
        m_missRegion.SizeInBytes = missSize;
        m_missRegion.StrideInBytes = missExports.empty() ? 0 : recordSize;
        m_hitRegion.StartAddress = base + rayGenSize + missSize;
        m_hitRegion.SizeInBytes = hitSize;
        m_hitRegion.StrideInBytes = hitGroupExports.empty() ? 0 : recordSize;
        m_callableRegion = {};
    }

    D3D12_DISPATCH_RAYS_DESC Dx12PipelineImpl::GetDispatchRaysDesc(uint32_t width, uint32_t height, uint32_t depth) const
    {
        D3D12_DISPATCH_RAYS_DESC desc{};
        desc.RayGenerationShaderRecord = m_rayGenRegion;
        desc.MissShaderTable = m_missRegion;
        desc.HitGroupTable = m_hitRegion;
        desc.CallableShaderTable = m_callableRegion;
        desc.Width = width;
        desc.Height = height;
        desc.Depth = depth;
        return desc;
    }
} // namespace pe

#endif // PE_WIN32
