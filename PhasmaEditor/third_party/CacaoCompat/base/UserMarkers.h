#pragma once

// Stub for AMD Cauldron's debug-marker class. Pulled in by FFX-CACAO's
// ffx_cacao_impl.cpp on the D3D12 path because it hardcodes
// FFX_CACAO_ENABLE_CAULDRON_DEBUG, but the Cauldron framework itself
// is not vendored here. Engine-side debug markers are wired through
// CommandBuffer::BeginDebugRegion / InsertDebugLabel / EndDebugRegion.

#include <d3d12.h>

namespace CAULDRON_DX12
{
    class UserMarker
    {
    public:
        UserMarker(ID3D12GraphicsCommandList *, const char *) {}
        ~UserMarker() = default;
    };
} // namespace CAULDRON_DX12
