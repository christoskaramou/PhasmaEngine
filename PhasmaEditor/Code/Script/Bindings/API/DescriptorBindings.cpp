#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Descriptor.h"
#include "API/AccelerationStructure.h"
#include "API/Image.h"
#include "API/Buffer.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::DescriptorType> s_descriptorTypeMap = {
        {"sampler", vk::DescriptorType::eSampler},
        {"combined_image_sampler", vk::DescriptorType::eCombinedImageSampler},
        {"sampled_image", vk::DescriptorType::eSampledImage},
        {"storage_image", vk::DescriptorType::eStorageImage},
        {"uniform_buffer", vk::DescriptorType::eUniformBuffer},
        {"storage_buffer", vk::DescriptorType::eStorageBuffer},
        {"uniform_buffer_dynamic", vk::DescriptorType::eUniformBufferDynamic},
        {"storage_buffer_dynamic", vk::DescriptorType::eStorageBufferDynamic},
        {"input_attachment", vk::DescriptorType::eInputAttachment},
        {"acceleration_structure", vk::DescriptorType::eAccelerationStructureKHR},
    };

    static const std::unordered_map<std::string_view, vk::ShaderStageFlagBits> s_shaderStageMap = {
        {"vertex", vk::ShaderStageFlagBits::eVertex},
        {"fragment", vk::ShaderStageFlagBits::eFragment},
        {"compute", vk::ShaderStageFlagBits::eCompute},
        {"geometry", vk::ShaderStageFlagBits::eGeometry},
        {"tessellation_control", vk::ShaderStageFlagBits::eTessellationControl},
        {"tessellation_evaluation", vk::ShaderStageFlagBits::eTessellationEvaluation},
        {"raygen", vk::ShaderStageFlagBits::eRaygenKHR},
        {"any_hit", vk::ShaderStageFlagBits::eAnyHitKHR},
        {"closest_hit", vk::ShaderStageFlagBits::eClosestHitKHR},
        {"miss", vk::ShaderStageFlagBits::eMissKHR},
        {"intersection", vk::ShaderStageFlagBits::eIntersectionKHR},
        {"all", vk::ShaderStageFlagBits::eAll},
    };

    static const std::unordered_map<std::string_view, vk::ImageLayout> s_descImageLayoutMap = {
        {"undefined", vk::ImageLayout::eUndefined},
        {"general", vk::ImageLayout::eGeneral},
        {"shader_read", vk::ImageLayout::eShaderReadOnlyOptimal},
        {"color_attachment", vk::ImageLayout::eColorAttachmentOptimal},
        {"depth_attachment", vk::ImageLayout::eDepthStencilAttachmentOptimal},
    };

    static struct DescriptorBindings
    {
        DescriptorBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // DescriptorPool
                sol::usertype<DescriptorPool> poolType = lua.new_usertype<DescriptorPool>("DescriptorPool", sol::no_constructor);

                // DescriptorLayout
                sol::usertype<DescriptorLayout> layoutType = lua.new_usertype<DescriptorLayout>("DescriptorLayout", sol::no_constructor);
                layoutType["get_variable_count"] = sol::property(&DescriptorLayout::GetVariableCount);
                layoutType["is_push_descriptor"] = sol::property(&DescriptorLayout::IsPushDescriptor);

                // DescriptorLayout static: get_or_create, calculate_hash, clear_cache
                lua.set_function("descriptor_layout_get_or_create", [](sol::table bindingsTable, const std::string &stage, sol::optional<bool> pushDescriptor) -> DescriptorLayout * {
                    std::vector<DescriptorBindingInfo> bindingInfos;
                    bindingInfos.reserve(bindingsTable.size());
                    for (size_t i = 1; i <= bindingsTable.size(); i++)
                    {
                        sol::table entry = bindingsTable.get<sol::table>(i);
                        DescriptorBindingInfo info{};
                        info.binding = entry.get<uint32_t>("binding");
                        info.count = entry.get_or<uint32_t>("count", 1);
                        info.type = Lookup(entry.get_or<std::string>("type", "combined_image_sampler"), s_descriptorTypeMap, vk::DescriptorType::eCombinedImageSampler);
                        info.imageLayout = Lookup(entry.get_or<std::string>("layout", "undefined"), s_descImageLayoutMap, vk::ImageLayout::eUndefined);
                        info.bindless = entry.get_or("bindless", false);
                        info.name = entry.get_or<std::string>("name", "");
                        bindingInfos.push_back(info);
                    }
                    vk::ShaderStageFlags stageFlags = LookupFlags<vk::ShaderStageFlags>(stage, s_shaderStageMap);
                    return DescriptorLayout::GetOrCreate(bindingInfos, stageFlags, pushDescriptor.value_or(false));
                });
                lua.set_function("descriptor_layout_clear_cache", []() { DescriptorLayout::ClearCache(); });

                // Descriptor
                sol::usertype<Descriptor> descType = lua.new_usertype<Descriptor>("Descriptor", sol::no_constructor);

                // Factory
                lua.set_function("create_descriptor", [](sol::table bindingsTable, const std::string &stage, const std::string &name, sol::optional<bool> pushDescriptor) -> Descriptor * {
                    std::vector<DescriptorBindingInfo> bindingInfos;
                    bindingInfos.reserve(bindingsTable.size());
                    for (size_t i = 1; i <= bindingsTable.size(); i++)
                    {
                        sol::table entry = bindingsTable.get<sol::table>(i);
                        DescriptorBindingInfo info{};
                        info.binding = entry.get<uint32_t>("binding");
                        info.count = entry.get_or<uint32_t>("count", 1);
                        info.type = Lookup(entry.get_or<std::string>("type", "combined_image_sampler"), s_descriptorTypeMap, vk::DescriptorType::eCombinedImageSampler);
                        info.imageLayout = Lookup(entry.get_or<std::string>("layout", "undefined"), s_descImageLayoutMap, vk::ImageLayout::eUndefined);
                        info.bindless = entry.get_or("bindless", false);
                        info.name = entry.get_or<std::string>("name", "");
                        bindingInfos.push_back(info);
                    }
                    vk::ShaderStageFlags stageFlags = LookupFlags<vk::ShaderStageFlags>(stage, s_shaderStageMap);
                    return Descriptor::Create(bindingInfos, stageFlags, pushDescriptor.value_or(false), name);
                });
                lua.set_function("destroy_descriptor", [](Descriptor *desc) { Descriptor::Destroy(desc); });

                // SetImageViews
                descType["set_image_views"] = [](Descriptor &desc, uint32_t binding, sol::table viewsTable, sol::optional<sol::table> samplersTable) {
                    std::vector<ImageView *> views;
                    views.reserve(viewsTable.size());
                    for (auto &[k, v] : viewsTable)
                    {
                        if (v.is<ImageView *>())
                            views.push_back(v.as<ImageView *>());
                    }
                    std::vector<Sampler *> samplers;
                    if (samplersTable)
                    {
                        samplers.reserve(samplersTable->size());
                        for (auto &[k, v] : *samplersTable)
                        {
                            if (v.is<Sampler *>())
                                samplers.push_back(v.as<Sampler *>());
                        }
                    }
                    desc.SetImageViews(binding, views, samplers);
                };
                // SetImageView
                descType["set_image_view"] = sol::overload(
                    [](Descriptor &desc, uint32_t binding, ImageView *view) { desc.SetImageView(binding, view); },
                    [](Descriptor &desc, uint32_t binding, ImageView *view, Sampler *sampler) { desc.SetImageView(binding, view, sampler); });
                // SetBuffers
                descType["set_buffers"] = sol::overload(
                    [](Descriptor &desc, uint32_t binding, sol::table buffersTable) {
                        std::vector<Buffer *> buffers;
                        buffers.reserve(buffersTable.size());
                        for (auto &[k, v] : buffersTable)
                        {
                            if (v.is<Buffer *>())
                                buffers.push_back(v.as<Buffer *>());
                        }
                        desc.SetBuffers(binding, buffers);
                    },
                    [](Descriptor &desc, uint32_t binding, sol::table buffersTable, sol::table offsetsTable, sol::table rangesTable) {
                        std::vector<Buffer *> buffers;
                        buffers.reserve(buffersTable.size());
                        for (auto &[k, v] : buffersTable)
                        {
                            if (v.is<Buffer *>())
                                buffers.push_back(v.as<Buffer *>());
                        }
                        std::vector<uint64_t> offsets;
                        offsets.reserve(offsetsTable.size());
                        for (auto &[k, v] : offsetsTable)
                            offsets.push_back(v.as<uint64_t>());
                        std::vector<uint64_t> ranges;
                        ranges.reserve(rangesTable.size());
                        for (auto &[k, v] : rangesTable)
                            ranges.push_back(v.as<uint64_t>());
                        desc.SetBuffers(binding, buffers, offsets, ranges);
                    });
                // SetBuffer
                descType["set_buffer"] = sol::overload(
                    [](Descriptor &desc, uint32_t binding, Buffer &buffer) { desc.SetBuffer(binding, &buffer); },
                    [](Descriptor &desc, uint32_t binding, Buffer &buffer, uint64_t offset) { desc.SetBuffer(binding, &buffer, offset); },
                    [](Descriptor &desc, uint32_t binding, Buffer &buffer, uint64_t offset, uint64_t range) { desc.SetBuffer(binding, &buffer, offset, range); });
                // SetSamplers
                descType["set_samplers"] = [](Descriptor &desc, uint32_t binding, sol::table samplersTable) {
                    std::vector<Sampler *> samplers;
                    samplers.reserve(samplersTable.size());
                    for (auto &[k, v] : samplersTable)
                    {
                        if (v.is<Sampler *>())
                            samplers.push_back(v.as<Sampler *>());
                    }
                    desc.SetSamplers(binding, samplers);
                };
                // SetSampler
                descType["set_sampler"] = [](Descriptor &desc, uint32_t binding, Sampler *sampler) {
                    desc.SetSampler(binding, sampler);
                };
                // SetAccelerationStructure
                descType["set_acceleration_structure"] = [](Descriptor &desc, uint32_t binding, AccelerationStructure &as) {
                    desc.SetAccelerationStructure(binding, as.ApiHandle());
                };
                // Update
                descType["update"] = &Descriptor::Update;
                // GetPool
                descType["get_pool"] = &Descriptor::GetPool;
                // GetLayout
                descType["get_layout"] = &Descriptor::GetLayout;
                // GetStage
                descType["get_stage"] = [](Descriptor &d) -> uint32_t {
                    return static_cast<uint32_t>(static_cast<VkShaderStageFlags>(d.GetStage()));
                };
                // GetBoundResources
                descType["get_bound_resources"] = [&lua](Descriptor &d) -> sol::table {
                    sol::table t = lua.create_table();
                    auto &resources = d.GetBoundResources();
                    for (size_t i = 0; i < resources.size(); i++)
                    {
                        sol::table entry = lua.create_table();
                        entry["binding"] = resources[i].binding;
                        entry["buffer_count"] = static_cast<uint32_t>(resources[i].buffers.size());
                        entry["view_count"] = static_cast<uint32_t>(resources[i].views.size());
                        entry["sampler_count"] = static_cast<uint32_t>(resources[i].samplers.size());
                        t[i + 1] = entry;
                    }
                    return t;
                };
                // GetBindingInfos
                descType["get_binding_infos"] = [&lua](Descriptor &d) -> sol::table {
                    sol::table t = lua.create_table();
                    auto &infos = d.GetBindingInfos();
                    for (size_t i = 0; i < infos.size(); i++)
                    {
                        sol::table entry = lua.create_table();
                        entry["binding"] = infos[i].binding;
                        entry["count"] = infos[i].count;
                        entry["type"] = static_cast<uint32_t>(infos[i].type);
                        entry["bindless"] = infos[i].bindless;
                        entry["name"] = infos[i].name;
                        t[i + 1] = entry;
                    }
                    return t;
                };

                // ImageView
                sol::usertype<ImageView> ivType = lua.new_usertype<ImageView>("ImageView", sol::no_constructor);
                ivType["get_parent"] = &ImageView::GetParent;

                // Sampler
                sol::usertype<Sampler> samplerType = lua.new_usertype<Sampler>("Sampler", sol::no_constructor); });
        }
    } s_descriptorBindings;
} // namespace pe
#endif
