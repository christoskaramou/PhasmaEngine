#include "ECS/Component.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Pipeline.h"
#include "API/RHI.h"

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
}
