#include "ECS/Component.h"
#include "API/Command.h"
#include "API/Pipeline.h"

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
} // namespace pe
