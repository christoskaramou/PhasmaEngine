#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"

namespace pe
{
    static struct QueueBindings
    {
        QueueBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // --- CommandPool ---
                sol::usertype<CommandPool> cpType = lua.new_usertype<CommandPool>("CommandPool", sol::no_constructor);

                // GetQueue
                cpType["get_queue"] = &CommandPool::GetQueue;

                // GetFlags
                cpType["get_flags"] = [](CommandPool &cp) -> uint32_t {
                    return static_cast<uint32_t>(static_cast<vk::CommandPoolCreateFlags::MaskType>(cp.GetFlags()));
                };

                // Reset
                cpType["reset"] = &CommandPool::Reset;

                // --- Queue ---
                sol::usertype<Queue> queueType = lua.new_usertype<Queue>("Queue", sol::no_constructor);

                // Submit
                queueType["submit"] = sol::overload(
                    [](Queue &q, CommandBuffer *cmd) {
                        q.Submit(1, &cmd, nullptr, nullptr);
                    },
                    [](Queue &q, CommandBuffer *cmd, Semaphore *wait, Semaphore *signal) {
                        q.Submit(1, &cmd, wait, signal);
                    });

                // Present
                queueType["present"] = [](Queue &q, Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait) {
                    q.Present(swapchain, imageIndex, wait);
                };

                // Wait
                queueType["wait"] = &Queue::Wait;

                // WaitIdle
                queueType["wait_idle"] = &Queue::WaitIdle;

                // BeginDebugRegion
                queueType["begin_debug_region"] = &Queue::BeginDebugRegion;

                // InsertDebugLabel
                queueType["insert_debug_label"] = &Queue::InsertDebugLabel;

                // EndDebugRegion
                queueType["end_debug_region"] = &Queue::EndDebugRegion;

                // GetFamilyId
                queueType["get_family_id"] = sol::property(&Queue::GetFamilyId);

                // GetSubmissionsSemaphore
                queueType["get_submissions_semaphore"] = &Queue::GetSubmissionsSemaphore;

                // AcquireCommandBuffer
                queueType["acquire_command_buffer"] = [](Queue &q) -> CommandBuffer * {
                    return q.AcquireCommandBuffer();
                };

                // ReturnCommandBuffer
                queueType["return_command_buffer"] = &Queue::ReturnCommandBuffer;

                // GetSubmissionCount
                queueType["get_submission_count"] = sol::property(&Queue::GetSubmissionCount); });
        }
    } s_queueBindings;
} // namespace pe
#endif
