#include "PeSurfaceNodeSlang.h"
#include <MaterialXGenHw/HwConstants.h>
#include <MaterialXGenHw/HwShaderGenerator.h>

namespace pe
{
    namespace mx = MaterialX;

    // Mirrors HwSurfaceNode::emitLightLoop (MaterialX 1.39.5, HwSurfaceNode.cpp:263-313) with PE's lights in place of
    // MaterialX's LightData array. The BSDF responses already fold in N.L, so the light only supplies radiance.
    void PeSurfaceNodeSlang::emitLightLoop(const mx::ShaderNode &node, mx::GenContext &context, mx::ShaderStage &stage,
                                           const std::string &outColor) const
    {
        const auto &shadergen = static_cast<const mx::HwShaderGenerator &>(context.getShaderGenerator());
        const mx::ShaderInput *bsdfInput = node.getInput(getBsdfInputName());
        const mx::ShaderNode *bsdf = bsdfInput ? bsdfInput->getConnectedSibling() : nullptr;
        if (!bsdf)
            return;

        const std::string prefix = shadergen.getVertexDataPrefix(stage.getInputBlock(mx::HW::VERTEX_DATA));

        shadergen.emitComment("PhasmaEngine light loop", stage);
        shadergen.emitLine("for (uint peLight = 0; peLight < pe_light_count(); ++peLight)", stage, false);
        shadergen.emitScopeBegin(stage);
        shadergen.emitLine("float3 peRadiance", stage);
        shadergen.emitLine("pe_light_sample(peLight, " + prefix + mx::HW::T_POSITION_WORLD + ", L, peRadiance, occlusion)", stage);

        if (bsdf->hasClassification(mx::ShaderNode::Classification::BSDF_R))
        {
            shadergen.emitLine("ClosureData closureData = makeClosureData(CLOSURE_TYPE_REFLECTION, L, V, N, P, occlusion)", stage);
            shadergen.emitFunctionCall(*bsdf, context, stage);
        }
        else
        {
            shadergen.emitLineBegin(stage);
            shadergen.emitOutput(bsdf->getOutput(), true, true, context, stage);
            shadergen.emitLineEnd(stage);
        }

        shadergen.emitLine(outColor + " += peRadiance * " + bsdf->getOutput()->getVariable() + ".response", stage);
        shadergen.emitLine("occlusion = 1.0", stage);
        shadergen.emitScopeEnd(stage);
    }
} // namespace pe
