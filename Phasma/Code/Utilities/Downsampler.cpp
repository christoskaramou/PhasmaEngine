#include "Utilities/Downsampler.h"

namespace pe
{
    Downsampler::Downsampler()
    {
        DSet = {};
        pipeline = nullptr;
    }

    Downsampler::~Downsampler()
    {
    }

    void Downsampler::Init()
    {
        sizeof(PushConstants);
    }

    void Downsampler::UpdatePipelineInfo()
    {
    }

    void Downsampler::CreateUniforms(CommandBuffer *cmd)
    {
    }

    void Downsampler::UpdateDescriptorSets()
    {
    }

    void Downsampler::Update(Camera *camera)
    {
    }

    void Downsampler::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
    }

    void Downsampler::Resize(uint32_t width, uint32_t height)
    {
    }

    void Downsampler::Destroy()
    {
    }
}
