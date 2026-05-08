#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeAccelerationStructureBuildFlags> s_asBuildFlagsMap = {
        {"allow_update", PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE},
        {"allow_compaction", PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_COMPACTION},
        {"prefer_fast_trace", PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE},
        {"prefer_fast_build", PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_BUILD},
        {"low_memory", PE_ACCELERATION_STRUCTURE_BUILD_LOW_MEMORY},
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
                                     LookupFlags<PeAccelerationStructureBuildFlags>(flags, s_asBuildFlagsMap));
                    },
                    [](AccelerationStructure &as, CommandBuffer *cmd, uint32_t instanceCount,
                       Buffer *instanceBuffer, const std::string &flags, uint64_t scratchAddress) {
                        as.BuildTLAS(cmd, instanceCount, instanceBuffer,
                                     LookupFlags<PeAccelerationStructureBuildFlags>(flags, s_asBuildFlagsMap),
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
