#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Buffer.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::BufferUsageFlags2> s_bufferUsageMap = {
        {"uniform", vk::BufferUsageFlagBits2::eUniformBuffer},
        {"storage", vk::BufferUsageFlagBits2::eStorageBuffer},
        {"vertex", vk::BufferUsageFlagBits2::eVertexBuffer},
        {"index", vk::BufferUsageFlagBits2::eIndexBuffer},
        {"indirect", vk::BufferUsageFlagBits2::eIndirectBuffer},
        {"transfer_src", vk::BufferUsageFlagBits2::eTransferSrc},
        {"transfer_dst", vk::BufferUsageFlagBits2::eTransferDst},
        {"device_address", vk::BufferUsageFlagBits2::eShaderDeviceAddress},
        {"acceleration_structure_storage", vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR},
        {"acceleration_structure_input", vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR},
        {"shader_binding_table", vk::BufferUsageFlagBits2::eShaderBindingTableKHR},
    };

    static const std::unordered_map<std::string_view, VmaAllocationCreateFlags> s_vmaFlagsMap = {
        {"host_write", VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT},
        {"host_read", VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT},
        {"mapped", VMA_ALLOCATION_CREATE_MAPPED_BIT},
        {"dedicated", VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT},
    };

    static struct BufferBindings
    {
        BufferBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Buffer> bufType = lua.new_usertype<Buffer>("Buffer", sol::no_constructor);

                // Factory
                lua.set_function("create_buffer", [](size_t size, const std::string &usage, const std::string &alloc, const std::string &name) -> Buffer * {
                    return Buffer::Create(size,
                        LookupFlags<vk::BufferUsageFlags2>(usage, s_bufferUsageMap),
                        LookupFlags<VmaAllocationCreateFlags>(alloc, s_vmaFlagsMap),
                        name);
                });
                lua.set_function("destroy_buffer", [](Buffer *buf) {
                    Buffer::Destroy(buf);
                });

                // Mapping
                bufType["map"] = &Buffer::Map;
                bufType["unmap"] = &Buffer::Unmap;
                bufType["flush"] = sol::overload(
                    [](Buffer &buf) { buf.Flush(); },
                    [](Buffer &buf, size_t size, size_t offset) { buf.Flush(size, offset); });

                // Operations
                bufType["zero"] = &Buffer::Zero;

                // set_data({values}, "type", offset) - pack array of same-typed values
                bufType["set_data"] = [](Buffer &buf, sol::table data, const std::string &type, sol::optional<size_t> dstOffset) {
                    auto it = s_packTypeMap.find(std::string_view(type));
                    if (it == s_packTypeMap.end()) return;
                    size_t offset = dstOffset.value_or(0);
                    std::vector<uint8_t> bytes;
                    for (size_t i = 1; i <= data.size(); i++)
                        PackValue(bytes, data, i, it->second);
                    if (bytes.empty() || offset + bytes.size() > buf.Size()) return;
                    BufferRange range{bytes.data(), bytes.size(), offset};
                    buf.Copy(1, &range, true);
                };

                // set_struct({{type, value}, ...}, offset) - pack mixed types sequentially
                bufType["set_struct"] = [](Buffer &buf, sol::table entries, sol::optional<size_t> dstOffset) {
                    size_t offset = dstOffset.value_or(0);
                    std::vector<uint8_t> bytes;
                    for (size_t i = 1; i <= entries.size(); i++)
                    {
                        sol::table entry = entries.get<sol::table>(i);
                        std::string typeStr = entry.get<std::string>(1);
                        auto it = s_packTypeMap.find(std::string_view(typeStr));
                        if (it == s_packTypeMap.end()) continue;
                        for (size_t j = 2; j <= entry.size(); j++)
                            PackValue(bytes, entry, j, it->second);
                    }
                    if (bytes.empty() || offset + bytes.size() > buf.Size()) return;
                    BufferRange range{bytes.data(), bytes.size(), offset};
                    buf.Copy(1, &range, true);
                };

                // get_data(count, "type", offset) - read array of same-typed values from mapped buffer
                bufType["get_data"] = [&lua](Buffer &buf, size_t count, const std::string &type, sol::optional<size_t> srcOffset) -> sol::table {
                    sol::table result = lua.create_table();
                    auto it = s_packTypeMap.find(std::string_view(type));
                    if (it == s_packTypeMap.end() || !buf.Data()) return result;
                    size_t offset = srcOffset.value_or(0);

                    auto readValues = [&]<typename T>(size_t elemSize) {
                        if (offset + count * elemSize > buf.Size()) return;
                        const uint8_t *ptr = static_cast<const uint8_t *>(buf.Data()) + offset;
                        for (size_t i = 0; i < count; i++)
                        {
                            T v;
                            memcpy(&v, ptr + i * elemSize, elemSize);
                            result[i + 1] = v;
                        }
                    };

                    switch (it->second)
                    {
                    case PackType::Float:  readValues.template operator()<float>(sizeof(float)); break;
                    case PackType::Double: readValues.template operator()<double>(sizeof(double)); break;
                    case PackType::Int:    readValues.template operator()<int32_t>(sizeof(int32_t)); break;
                    case PackType::Uint:   readValues.template operator()<uint32_t>(sizeof(uint32_t)); break;
                    case PackType::Vec2:   readValues.template operator()<vec2>(sizeof(vec2)); break;
                    case PackType::Vec3:   readValues.template operator()<vec3>(sizeof(vec3)); break;
                    case PackType::Vec4:   readValues.template operator()<vec4>(sizeof(vec4)); break;
                    case PackType::Mat4:   readValues.template operator()<mat4>(sizeof(mat4)); break;
                    }
                    return result;
                };

                // Properties
                bufType["size"] = sol::property(&Buffer::Size);
                bufType["device_address"] = sol::property(&Buffer::GetDeviceAddress);
                bufType["get_track_info"] = [&lua](Buffer &buf) -> sol::table {
                    auto &info = buf.GetTrackInfo();
                    sol::table t = lua.create_table();
                    t["queue_family_index"] = info.queueFamilyIndex;
                    t["offset"] = static_cast<uint32_t>(info.offset);
                    t["size"] = static_cast<uint32_t>(info.size == VK_WHOLE_SIZE ? buf.Size() : info.size);
                    return t;
                }; });
        }
    } s_bufferBindings;
} // namespace pe
#endif
