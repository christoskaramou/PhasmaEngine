#include "ECS/Component.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RHI.h"

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
