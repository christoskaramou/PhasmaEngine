#include "Particles/Backends/ParticleBufferBackend.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"

namespace pe
{
    namespace ParticleBufferBackend
    {
        PeMemoryUsage ParticleBufferMemoryUsage()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12 ? PE_MEMORY_USAGE_GPU_ONLY : PE_MEMORY_USAGE_CPU_TO_GPU;
        }

        void ZeroParticleBuffer(Buffer *buffer)
        {
            if (!buffer)
                return;

            if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            {
                Queue *queue = RHII.GetMainQueue();
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                cmd->FillBuffer(buffer, 0, buffer->Size(), 0);
                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
                return;
            }

            buffer->Map();
            buffer->Zero();
            buffer->Flush();
            buffer->Unmap();
        }

        void ZeroParticleBufferRange(Buffer *buffer, size_t byteOffset, size_t byteSize)
        {
            if (!buffer || byteSize == 0 || byteOffset >= buffer->Size())
                return;

            byteSize = std::min(byteSize, buffer->Size() - byteOffset);

            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->FillBuffer(buffer, byteOffset, byteSize, 0);
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
        }
    } // namespace ParticleBufferBackend
} // namespace pe
