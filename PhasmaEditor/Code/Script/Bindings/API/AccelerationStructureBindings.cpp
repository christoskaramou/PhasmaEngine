#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Vulkan/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::BuildAccelerationStructureFlagBitsKHR> s_asBuildFlagsMap = {
        {"allow_update", vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate},
        {"allow_compaction", vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction},
        {"prefer_fast_trace", vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace},
        {"prefer_fast_build", vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild},
        {"low_memory", vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory},
    };

    static struct AccelerationStructureBindings
    {
        AccelerationStructureBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<AccelerationStructure> asType = lua.new_usertype<AccelerationStructure>("AccelerationStructure", sol::no_constructor);

                // Factory
                lua.set_function("create_acceleration_structure", sol::overload(
                    [](const std::string &name) -> AccelerationStructure * {
                        return AccelerationStructure::Create(name);
                    },
                    [](const std::string &name, Buffer *buffer, uint64_t offset) -> AccelerationStructure * {
                        return AccelerationStructure::Create(name, buffer, offset);
                    }));
                lua.set_function("destroy_acceleration_structure", [](AccelerationStructure *as) {
                    if (as) AccelerationStructure::Destroy(as);
                });

                // BuildTLAS
                asType["build_tlas"] = sol::overload(
                    [](AccelerationStructure &as, CommandBuffer *cmd, uint32_t instanceCount,
                       Buffer *instanceBuffer, const std::string &flags) {
                        as.BuildTLAS(cmd, instanceCount, instanceBuffer,
                                     LookupFlags<vk::BuildAccelerationStructureFlagsKHR>(flags, s_asBuildFlagsMap));
                    },
                    [](AccelerationStructure &as, CommandBuffer *cmd, uint32_t instanceCount,
                       Buffer *instanceBuffer, const std::string &flags, uint64_t scratchAddress) {
                        as.BuildTLAS(cmd, instanceCount, instanceBuffer,
                                     LookupFlags<vk::BuildAccelerationStructureFlagsKHR>(flags, s_asBuildFlagsMap),
                                     scratchAddress);
                    });

                // UpdateTLAS
                asType["update_tlas"] = sol::overload(
                    [](AccelerationStructure &as, CommandBuffer *cmd, uint32_t instanceCount,
                       Buffer *instanceBuffer) {
                        as.UpdateTLAS(cmd, instanceCount, instanceBuffer);
                    },
                    [](AccelerationStructure &as, CommandBuffer *cmd, uint32_t instanceCount,
                       Buffer *instanceBuffer, uint64_t scratchAddress) {
                        as.UpdateTLAS(cmd, instanceCount, instanceBuffer, scratchAddress);
                    });

                // GetDeviceAddress
                asType["get_device_address"] = sol::property(&AccelerationStructure::GetDeviceAddress); });
        }
    } s_accelerationStructureBindings;
} // namespace pe
