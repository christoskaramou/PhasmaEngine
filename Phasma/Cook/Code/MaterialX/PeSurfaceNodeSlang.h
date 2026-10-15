#pragma once

#include <MaterialXGenHw/Nodes/HwSurfaceNode.h>

namespace pe
{
    // MaterialX's surface node with PE's light loop: lights, attenuation and shadows come from the engine
    // (pe_light_sample); MaterialX's BSDF closures are evaluated unchanged. Same seam as Hydra Storm / Maya.
    class PeSurfaceNodeSlang : public MaterialX::HwSurfaceNode
    {
    public:
        static MaterialX::ShaderNodeImplPtr create() { return std::make_shared<PeSurfaceNodeSlang>(); }

        void emitLightLoop(const MaterialX::ShaderNode &node, MaterialX::GenContext &context, MaterialX::ShaderStage &stage,
                           const std::string &outColor) const override;
    };
} // namespace pe
