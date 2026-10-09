#include "API/Command.h"
#include "API/Pipeline.h"
#include "API/RenderGraph.h"

namespace pe
{
    IRenderPassComponent::IRenderPassComponent()
        : m_attachments{},
          m_passInfo{std::make_shared<PassInfo>()}
    {
    }

    IRenderPassComponent::~IRenderPassComponent()
    {
    }

    void IRenderPassComponent::DeclareOutputs(RGBuilder &builder)
    {
        for (auto &att : m_attachments)
        {
            if (!att.image)
                continue;
            const bool loads = att.loadOp == PE_LOAD_OP_LOAD;
            if (::PeFormatHasDepth(att.image->GetFormat()))
                builder.OutputDepth(att.image, loads);
            else
                builder.OutputColor(att.image, loads);
        }
    }
} // namespace pe
