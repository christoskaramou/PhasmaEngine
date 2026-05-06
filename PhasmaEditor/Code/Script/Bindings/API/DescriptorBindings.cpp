#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Descriptor.h"
#include "API/Vulkan/AccelerationStructure.h"
#include "API/Image.h"
#include "API/Buffer.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeBindingType> s_descriptorTypeMap = {
        {"sampler", PE_DESCRIPTOR_TYPE_SAMPLER},
        {"combined_image_sampler", PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
        {"sampled_image", PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        {"storage_image", PE_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {"uniform_buffer", PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        {"storage_buffer", PE_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        {"structured_buffer", PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER},
        {"byte_address_buffer", PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER},
        {"uniform_buffer_dynamic", PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
        {"storage_buffer_dynamic", PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC},
        {"input_attachment", PE_DESCRIPTOR_TYPE_INPUT_ATTACHMENT},
        {"acceleration_structure", PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE},
    };

    static const std::unordered_map<std::string_view, PeShaderStageFlags> s_shaderStageMap = {
        {"vertex", PE_SHADER_STAGE_VERTEX},
        {"fragment", PE_SHADER_STAGE_FRAGMENT},
        {"compute", PE_SHADER_STAGE_COMPUTE},
        {"geometry", PE_SHADER_STAGE_GEOMETRY},
        {"tessellation_control", PE_SHADER_STAGE_TESS_CONTROL},
        {"tessellation_evaluation", PE_SHADER_STAGE_TESS_EVALUATION},
        {"raygen", PE_SHADER_STAGE_RAYGEN_KHR},
        {"any_hit", PE_SHADER_STAGE_ANY_HIT_KHR},
        {"closest_hit", PE_SHADER_STAGE_CLOSEST_HIT_KHR},
        {"miss", PE_SHADER_STAGE_MISS_KHR},
        {"intersection", PE_SHADER_STAGE_INTERSECTION_KHR},
        {"all", PE_SHADER_STAGE_ALL},
    };

    static const std::unordered_map<std::string_view, PeImageLayout> s_descImageLayoutMap = {
        {"undefined", PE_IMAGE_LAYOUT_UNDEFINED},
        {"general", PE_IMAGE_LAYOUT_GENERAL},
        {"shader_read", PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {"color_attachment", PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {"depth_attachment", PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
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
                        info.type = Lookup(entry.get_or<std::string>("type", "combined_image_sampler"), s_descriptorTypeMap, PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                        info.imageLayout = Lookup(entry.get_or<std::string>("layout", "undefined"), s_descImageLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED);
                        info.bindless = entry.get_or("bindless", false);
                        info.structuredStride = entry.get_or("structured_stride", uint32_t{0});
                        info.name = entry.get_or<std::string>("name", "");
                        info.dxRegister = entry.get_or("dx_register", static_cast<uint32_t>(-1));
                        info.dxSpace = entry.get_or("dx_space", uint32_t{0});
                        bindingInfos.push_back(info);
                    }
                    PeShaderStageFlags stageFlags = LookupFlags<PeShaderStageFlags>(stage, s_shaderStageMap);
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
                        info.type = Lookup(entry.get_or<std::string>("type", "combined_image_sampler"), s_descriptorTypeMap, PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                        info.imageLayout = Lookup(entry.get_or<std::string>("layout", "undefined"), s_descImageLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED);
                        info.bindless = entry.get_or("bindless", false);
                        info.structuredStride = entry.get_or("structured_stride", uint32_t{0});
                        info.name = entry.get_or<std::string>("name", "");
                        info.dxRegister = entry.get_or("dx_register", static_cast<uint32_t>(-1));
                        info.dxSpace = entry.get_or("dx_space", uint32_t{0});
                        bindingInfos.push_back(info);
                    }
                    PeShaderStageFlags stageFlags = LookupFlags<PeShaderStageFlags>(stage, s_shaderStageMap);
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
                    desc.SetAccelerationStructure(binding, &as);
                };
                // Update
                descType["update"] = &Descriptor::Update;
                // GetPool
                descType["get_pool"] = &Descriptor::GetPool;
                // GetLayout
                descType["get_layout"] = &Descriptor::GetLayout;
                // GetStage
                descType["get_stage"] = [](Descriptor &d) -> uint32_t {
                    return static_cast<uint32_t>(d.GetStage());
                };
                // GetBoundResources
                descType["get_bound_resources"] = [](Descriptor &d, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
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
                descType["get_binding_infos"] = [](Descriptor &d, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    auto &infos = d.GetBindingInfos();
                    for (size_t i = 0; i < infos.size(); i++)
                    {
                        sol::table entry = lua.create_table();
                        entry["binding"] = infos[i].binding;
                        entry["count"] = infos[i].count;
                        entry["type"] = static_cast<uint32_t>(infos[i].type);
                        entry["structured_stride"] = infos[i].structuredStride;
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
