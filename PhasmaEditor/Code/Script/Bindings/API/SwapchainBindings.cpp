#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "API/Image.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"

namespace pe
{
    static struct SwapchainBindings
    {
        SwapchainBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Swapchain> swapType = lua.new_usertype<Swapchain>("Swapchain", sol::no_constructor);

                // AquireNextImage
                swapType["acquire_next_image"] = [](Swapchain &s, Semaphore *sem) -> uint32_t {
                    return s.AquireNextImage(sem);
                };

                // GetImage
                swapType["get_image"] = &Swapchain::GetImage;

                // GetImageCount
                swapType["get_image_count"] = sol::property(&Swapchain::GetImageCount); });
        }
    } s_swapchainBindings;
} // namespace pe
#endif
