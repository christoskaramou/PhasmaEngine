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
            if (::PeFormatHasDepth(att.image->GetFormat()))
                builder.OutputDepth(att.image);
            else
                builder.OutputColor(att.image);
        }
    }
} // namespace pe
