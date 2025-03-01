#include "Skybox.h"
#include "Renderer/Image.h"
#include "Renderer/Command.h"

namespace pe
{
    void SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames, uint32_t imageSideSize)
    {
        m_cubeMap = Image::LoadRGBA8Cubemap(cmd, textureNames, imageSideSize);

        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = ImageLayout::ShaderReadOnly;
        barrier.stageFlags = PipelineStage::FragmentShaderBit;
        barrier.accessMask = Access::ShaderReadBit;
        cmd->ImageBarrier(barrier);
    }

    void SkyBox::Destroy()
    {
        Image::Destroy(m_cubeMap);
    }
}
