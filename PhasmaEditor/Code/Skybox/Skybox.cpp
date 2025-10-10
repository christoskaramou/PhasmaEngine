#include "Skybox.h"
#include "API/Image.h"
#include "API/Command.h"

namespace pe
{
    void SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames, uint32_t imageSideSize)
    {
        m_cubeMap = Image::LoadRGBA8Cubemap(cmd, textureNames, imageSideSize);

        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);
    }

    void SkyBox::Destroy()
    {
        Image::Destroy(m_cubeMap);
    }
}
