#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Buffer.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeBufferUsageFlags> s_bufferUsageMap = {
        {"uniform", PE_BUFFER_USAGE_UNIFORM_BUFFER},
        {"storage", PE_BUFFER_USAGE_STORAGE_BUFFER},
        {"vertex", PE_BUFFER_USAGE_VERTEX_BUFFER},
        {"index", PE_BUFFER_USAGE_INDEX_BUFFER},
        {"indirect", PE_BUFFER_USAGE_INDIRECT_BUFFER},
        {"transfer_src", PE_BUFFER_USAGE_TRANSFER_SRC},
        {"transfer_dst", PE_BUFFER_USAGE_TRANSFER_DST},
        {"device_address", PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS},
        {"acceleration_structure_storage", PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR},
        {"acceleration_structure_input", PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR},
        {"shader_binding_table", PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR},
    };

    // Lua takes one memory-usage keyword. Pre-existing scripts that combined
    // VMA flag strings should pick the closest match here.
    static const std::unordered_map<std::string_view, PeMemoryUsage> s_memoryUsageMap = {
        {"gpu_only", PE_MEMORY_USAGE_GPU_ONLY},
        {"gpu_dedicated", PE_MEMORY_USAGE_GPU_ONLY_DEDICATED},
        {"cpu_to_gpu", PE_MEMORY_USAGE_CPU_TO_GPU},
        {"cpu_to_gpu_persistent", PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT},
        {"gpu_to_cpu", PE_MEMORY_USAGE_GPU_TO_CPU},
    };

    static struct BufferBindings
    {
        BufferBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Buffer> bufType = lua.new_usertype<Buffer>("Buffer", sol::no_constructor);

                // Factory
                lua.set_function("create_buffer", [](size_t size, const std::string &usage, const std::string &memoryUsage, const std::string &name) -> Buffer * {
                    auto memIt = s_memoryUsageMap.find(std::string_view(memoryUsage));
                    PeMemoryUsage mem = memIt == s_memoryUsageMap.end() ? PE_MEMORY_USAGE_GPU_ONLY : memIt->second;
                    return Buffer::Create({
                        .size = size,
                        .usage = LookupFlags<PeBufferUsageFlags>(usage, s_bufferUsageMap),
                        .memoryUsage = mem,
                        .name = name,
                    });
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
                bufType["get_data"] = [](Buffer &buf, size_t count, const std::string &type, sol::optional<size_t> srcOffset, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
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
                bufType["get_track_info"] = [](Buffer &buf, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &info = buf.GetTrackInfo();
                    sol::table t = lua.create_table();
                    t["queue_family_index"] = info.queueFamilyIndex;
                    t["offset"] = static_cast<uint32_t>(info.offset);
                    t["size"] = static_cast<uint32_t>(info.size == PE_WHOLE_SIZE ? buf.Size() : info.size);
                    return t;
                }; });
        }
    } s_bufferBindings;
} // namespace pe
