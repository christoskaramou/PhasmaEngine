#include "API/DX12/Dx12AccelerationStructure.h"

#if defined(PE_WIN32)

#include "API/Buffer.h"
#include "API/Command.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/RHI.h"

#include <algorithm>

namespace pe
{
    namespace
    {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ToDx12BuildFlags(PeAccelerationStructureBuildFlags flags)
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS dxFlags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE)
                dxFlags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
                    dxFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_COMPACTION)
                dxFlags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
                    dxFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION);
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE)
                dxFlags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
                    dxFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE);
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_BUILD)
                dxFlags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
                    dxFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD);
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_LOW_MEMORY)
                dxFlags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
                    dxFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY);
            return dxFlags;
        }

        bool SupportsDx12AccelerationStructures()
        {
            if (RHII.GetApi() != PE_GRAPHICS_API_DX12)
                return false;

            const auto *rhi = static_cast<const Dx12RhiImpl *>(RHII.GetImpl());
            return rhi && rhi->SupportsDxr();
        }

        Microsoft::WRL::ComPtr<ID3D12Device5> GetDx12Device5()
        {
            Microsoft::WRL::ComPtr<ID3D12Device5> device5;
            auto *device = GetDx12Device();
            if (!device)
                return device5;

            const HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device5));
            PE_ERROR_IF(FAILED(hr), "DX12 ray tracing requires ID3D12Device5 (0x%08X)", static_cast<unsigned>(hr));
            return device5;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> GetDx12CommandList4(CommandBuffer *cmd)
        {
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
            PE_ERROR_IF(!cmd, "DX12 acceleration-structure build requested without a command buffer");

            ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);
            PE_ERROR_IF(!list, "DX12 acceleration-structure build requested without a DX12 command list");

            const HRESULT hr = list->QueryInterface(IID_PPV_ARGS(&list4));
            PE_ERROR_IF(FAILED(hr), "DX12 ray tracing requires ID3D12GraphicsCommandList4 (0x%08X)", static_cast<unsigned>(hr));
            return list4;
        }

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> ToGeometryDescs(const std::vector<Dx12RtTriangleGeometry> &geometries)
        {
            PE_ERROR_IF(geometries.empty(), "DX12 BLAS build requested without geometry");

            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> descs;
            descs.reserve(geometries.size());

            for (const Dx12RtTriangleGeometry &geometry : geometries)
            {
                PE_ERROR_IF(geometry.vertexAddress == 0, "DX12 BLAS geometry has no vertex address");
                PE_ERROR_IF(geometry.vertexCount == 0, "DX12 BLAS geometry has no vertices");
                PE_ERROR_IF(geometry.vertexStride == 0, "DX12 BLAS geometry has no vertex stride");
                PE_ERROR_IF(geometry.indexCount == 0, "DX12 BLAS geometry has no indices");
                PE_ERROR_IF((geometry.indexCount % 3) != 0, "DX12 BLAS geometry index count must be triangle-aligned");

                D3D12_RAYTRACING_GEOMETRY_DESC desc{};
                desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                desc.Flags = geometry.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
                desc.Triangles.Transform3x4 = geometry.transformAddress;
                desc.Triangles.IndexFormat = pe_dx12::IndexFormat(geometry.indexType);
                desc.Triangles.VertexFormat = pe_dx12::Format(geometry.vertexFormat);
                desc.Triangles.IndexCount = geometry.indexCount;
                desc.Triangles.VertexCount = geometry.vertexCount;
                desc.Triangles.IndexBuffer = geometry.indexAddress;
                desc.Triangles.VertexBuffer.StartAddress = geometry.vertexAddress;
                desc.Triangles.VertexBuffer.StrideInBytes = geometry.vertexStride;
                PE_ERROR_IF(desc.Triangles.VertexFormat == DXGI_FORMAT_UNKNOWN, "DX12 BLAS geometry has unsupported vertex format");
                descs.push_back(desc);
            }

            return descs;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS BlasInputs(
            const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> &geometries,
            PeAccelerationStructureBuildFlags flags)
        {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags = ToDx12BuildFlags(flags);
            inputs.NumDescs = static_cast<UINT>(geometries.size());
            inputs.pGeometryDescs = geometries.data();
            return inputs;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS TlasInputs(uint32_t instanceCount,
                                                                        PeAccelerationStructureBuildFlags flags)
        {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags = ToDx12BuildFlags(flags);
            inputs.NumDescs = instanceCount;
            return inputs;
        }

        Dx12RtPrebuildInfo GetPrebuildInfo(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &inputs)
        {
            Dx12RtPrebuildInfo result{};
            if (!SupportsDx12AccelerationStructures())
            {
                PE_ERROR("DX12 acceleration-structure sizing requested without DXR support");
                return result;
            }

            Microsoft::WRL::ComPtr<ID3D12Device5> device5 = GetDx12Device5();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
            device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
            PE_ERROR_IF(info.ResultDataMaxSizeInBytes == 0, "DX12 acceleration-structure prebuild returned zero result size");

            result.resultDataMaxSize = info.ResultDataMaxSizeInBytes;
            result.buildScratchSize = info.ScratchDataSizeInBytes;
            result.updateScratchSize = info.UpdateScratchDataSizeInBytes;
            return result;
        }

        D3D12_GPU_VIRTUAL_ADDRESS PrepareStorage(AccelerationStructure *as,
                                                 const Dx12RtPrebuildInfo &prebuild,
                                                 bool keepUpdateScratch,
                                                 uint64_t scratchAddress)
        {
            PE_ERROR_IF(!as, "DX12 acceleration-structure build requested with a null destination");
            if (!Dx12AccelerationStructureAccess::IsExternalBuffer(as))
                Dx12AccelerationStructureAccess::CreateBuffer(as, prebuild.resultDataMaxSize);

            Buffer *buffer = Dx12AccelerationStructureAccess::GetBuffer(as);
            PE_ERROR_IF(!buffer, "DX12 acceleration-structure build has no destination buffer");

            const uint64_t resultAddress = buffer->GetDeviceAddress() + Dx12AccelerationStructureAccess::GetOffset(as);
            Dx12AccelerationStructureAccess::SetDeviceAddress(as, resultAddress);

            if (!scratchAddress)
            {
                const uint64_t scratchSize = keepUpdateScratch ? std::max(prebuild.buildScratchSize, prebuild.updateScratchSize) : prebuild.buildScratchSize;
                Dx12AccelerationStructureAccess::CreateScratchBuffer(as, scratchSize);
            }

            return resultAddress;
        }

        D3D12_GPU_VIRTUAL_ADDRESS ScratchAddress(AccelerationStructure *as, uint64_t scratchAddress)
        {
            if (scratchAddress)
                return scratchAddress;

            Buffer *scratch = Dx12AccelerationStructureAccess::GetScratchBuffer(as);
            PE_ERROR_IF(!scratch, "DX12 acceleration-structure build has no scratch buffer");
            return scratch->GetDeviceAddress();
        }

        void PrepareBuildBarriers(CommandBuffer *cmd, AccelerationStructure *as, Buffer *instanceBuffer)
        {
            PE_ERROR_IF(!cmd, "DX12 acceleration-structure build requested without a command buffer");

            Buffer *buffer = Dx12AccelerationStructureAccess::GetBuffer(as);
            PE_ERROR_IF(!buffer, "DX12 acceleration-structure build has no destination buffer");

            BufferBarrierInfo resultBarrier{};
            resultBarrier.buffer = buffer;
            resultBarrier.stageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
            resultBarrier.accessMask = PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
            cmd->BufferBarrier(resultBarrier);

            if (Buffer *scratch = Dx12AccelerationStructureAccess::GetScratchBuffer(as))
            {
                BufferBarrierInfo scratchBarrier{};
                scratchBarrier.buffer = scratch;
                scratchBarrier.stageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
                scratchBarrier.accessMask = PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE;
                cmd->BufferBarrier(scratchBarrier);
            }

            if (instanceBuffer)
            {
                BufferBarrierInfo instanceBarrier{};
                instanceBarrier.buffer = instanceBuffer;
                instanceBarrier.stageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
                instanceBarrier.accessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR;
                cmd->BufferBarrier(instanceBarrier);
            }

            Dx12CommandBufferImpl::From(cmd)->FlushBarriers();
        }

        void QueueBuildCompleteBarrier(CommandBuffer *cmd, bool readableByRayTracingShader)
        {
            MemoryBarrierInfo barrier{};
            barrier.srcStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
            barrier.srcAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
            barrier.dstStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
            barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR | PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
            if (readableByRayTracingShader)
                barrier.dstStageMask |= PE_STAGE_RAY_TRACING_SHADER_KHR;
            cmd->MemoryBarrier(barrier);
        }
    } // namespace

    Dx12RtPrebuildInfo GetDx12BlasPrebuildInfo(const std::vector<Dx12RtTriangleGeometry> &geometries,
                                               PeAccelerationStructureBuildFlags flags)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs = ToGeometryDescs(geometries);
        return GetPrebuildInfo(BlasInputs(geometryDescs, flags));
    }

    Dx12RtPrebuildInfo GetDx12TlasPrebuildInfo(uint32_t instanceCount,
                                               PeAccelerationStructureBuildFlags flags)
    {
        return GetPrebuildInfo(TlasInputs(instanceCount, flags));
    }

    void BuildDx12BLAS(AccelerationStructure *as,
                       CommandBuffer *cmd,
                       const std::vector<Dx12RtTriangleGeometry> &geometries,
                       PeAccelerationStructureBuildFlags flags,
                       uint64_t scratchAddress)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs = ToGeometryDescs(geometries);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = BlasInputs(geometryDescs, flags);
        const Dx12RtPrebuildInfo prebuild = GetPrebuildInfo(inputs);
        const bool keepUpdateScratch = (flags & PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE) != 0;
        const D3D12_GPU_VIRTUAL_ADDRESS resultAddress = PrepareStorage(as, prebuild, keepUpdateScratch, scratchAddress);
        const D3D12_GPU_VIRTUAL_ADDRESS buildScratchAddress = ScratchAddress(as, scratchAddress);
        PE_ERROR_IF(buildScratchAddress == 0, "DX12 BLAS build has no scratch address");

        PrepareBuildBarriers(cmd, as, nullptr);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.Inputs = inputs;
        buildDesc.DestAccelerationStructureData = resultAddress;
        buildDesc.ScratchAccelerationStructureData = buildScratchAddress;

        GetDx12CommandList4(cmd)->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        QueueBuildCompleteBarrier(cmd, false);
    }

    void BuildDx12TLAS(AccelerationStructure *as,
                       CommandBuffer *cmd,
                       uint32_t instanceCount,
                       Buffer *instanceBuffer,
                       PeAccelerationStructureBuildFlags flags,
                       uint64_t scratchAddress)
    {
        PE_ERROR_IF(!instanceBuffer, "DX12 TLAS build requested without an instance buffer");
        PE_ERROR_IF(instanceCount == 0, "DX12 TLAS build requested without instances");

        const PeAccelerationStructureBuildFlags buildFlags = flags | PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = TlasInputs(instanceCount, buildFlags);
        inputs.InstanceDescs = instanceBuffer->GetDeviceAddress();

        const Dx12RtPrebuildInfo prebuild = GetPrebuildInfo(inputs);
        const D3D12_GPU_VIRTUAL_ADDRESS resultAddress = PrepareStorage(as, prebuild, true, scratchAddress);
        const D3D12_GPU_VIRTUAL_ADDRESS buildScratchAddress = ScratchAddress(as, scratchAddress);
        PE_ERROR_IF(buildScratchAddress == 0, "DX12 TLAS build has no scratch address");

        PrepareBuildBarriers(cmd, as, instanceBuffer);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.Inputs = inputs;
        buildDesc.DestAccelerationStructureData = resultAddress;
        buildDesc.ScratchAccelerationStructureData = buildScratchAddress;

        GetDx12CommandList4(cmd)->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        QueueBuildCompleteBarrier(cmd, true);
    }

    void UpdateDx12TLAS(AccelerationStructure *as,
                        CommandBuffer *cmd,
                        uint32_t instanceCount,
                        Buffer *instanceBuffer,
                        uint64_t scratchAddress)
    {
        PE_ERROR_IF(!as || as->GetDeviceAddress() == 0, "DX12 TLAS update requested before build");
        PE_ERROR_IF(!instanceBuffer, "DX12 TLAS update requested without an instance buffer");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs =
            TlasInputs(instanceCount,
                       PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE |
                           PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE);
        inputs.Flags = static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(
            inputs.Flags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE);
        inputs.InstanceDescs = instanceBuffer->GetDeviceAddress();

        const D3D12_GPU_VIRTUAL_ADDRESS scratch = ScratchAddress(as, scratchAddress);
        PE_ERROR_IF(scratch == 0, "DX12 TLAS update has no scratch address");
        PrepareBuildBarriers(cmd, as, instanceBuffer);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.Inputs = inputs;
        buildDesc.SourceAccelerationStructureData = as->GetDeviceAddress();
        buildDesc.DestAccelerationStructureData = as->GetDeviceAddress();
        buildDesc.ScratchAccelerationStructureData = scratch;

        GetDx12CommandList4(cmd)->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        QueueBuildCompleteBarrier(cmd, true);
    }
} // namespace pe

#endif // PE_WIN32
