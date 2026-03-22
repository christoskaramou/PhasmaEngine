#pragma once

namespace pe
{
    // Path sandbox check: ensures a path is under Assets/ after normalization
    static bool IsUnderAssets(const std::filesystem::path &p)
    {
        std::error_code ec;
        std::string normalized = std::filesystem::weakly_canonical(p, ec).string();
        if (ec)
            normalized = p.lexically_normal().string();
        std::string assetsNorm = std::filesystem::weakly_canonical(Path::Assets, ec).string();
        if (ec)
            assetsNorm = std::filesystem::path(Path::Assets).lexically_normal().string();
        return normalized.find(assetsNorm) == 0;
    }

    // Resolve a user-supplied path to an absolute path under Assets/.
    // Handles cases where the user passes "Assets/Objects" (strips redundant "Assets/" prefix)
    // or just "Objects" (prepends Path::Assets).
    static std::filesystem::path ResolveAssetsPath(const std::string &path)
    {
        std::filesystem::path fpath(path);
        if (fpath.is_relative())
        {
            // Strip leading "Assets/" or "Assets\" to avoid double-nesting
            std::string p = path;
            if (p.rfind("Assets/", 0) == 0 || p.rfind("Assets\\", 0) == 0)
                p = p.substr(7);
            fpath = std::filesystem::path(Path::Assets) / p;
        }
        return fpath;
    }

    // Single-value string-to-enum lookup
    template <typename V>
    static V Lookup(const std::string &s, const std::unordered_map<std::string_view, V> &map, V fallback = {})
    {
        auto it = map.find(std::string_view(s));
        return it != map.end() ? it->second : fallback;
    }

    // Combinable flags lookup: splits by '|' delimiter
    template <typename T, typename V>
    static T LookupFlags(const std::string &s, const std::unordered_map<std::string_view, V> &map)
    {
        T flags{};
        size_t start = 0;
        while (start < s.size())
        {
            size_t end = s.find('|', start);
            if (end == std::string::npos)
                end = s.size();
            std::string_view token(s.data() + start, end - start);
            auto it = map.find(token);
            if (it != map.end())
                flags |= static_cast<T>(it->second);
            start = end + 1;
        }
        return flags;
    }

    // Type-safe packing for Lua-to-GPU data transfer
    enum class PackType : uint8_t
    {
        Float,
        Double,
        Int,
        Uint,
        Vec2,
        Vec3,
        Vec4,
        Mat4
    };

    inline const std::unordered_map<std::string_view, PackType> s_packTypeMap = {
        {"float", PackType::Float},
        {"double", PackType::Double},
        {"int", PackType::Int},
        {"uint", PackType::Uint},
        {"vec2", PackType::Vec2},
        {"vec3", PackType::Vec3},
        {"vec4", PackType::Vec4},
        {"mat4", PackType::Mat4},
    };

    template <typename T>
    static void PackAs(std::vector<uint8_t> &bytes, const sol::table &entry, size_t index)
    {
        T v = entry.get<T>(index);
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + sizeof(T));
    }

    static void PackValue(std::vector<uint8_t> &bytes, const sol::table &entry, size_t index, PackType type)
    {
        switch (type)
        {
        case PackType::Float:
            PackAs<float>(bytes, entry, index);
            break;
        case PackType::Double:
            PackAs<double>(bytes, entry, index);
            break;
        case PackType::Int:
            PackAs<int32_t>(bytes, entry, index);
            break;
        case PackType::Uint:
            PackAs<uint32_t>(bytes, entry, index);
            break;
        case PackType::Vec2:
            PackAs<vec2>(bytes, entry, index);
            break;
        case PackType::Vec3:
            PackAs<vec3>(bytes, entry, index);
            break;
        case PackType::Vec4:
            PackAs<vec4>(bytes, entry, index);
            break;
        case PackType::Mat4:
            PackAs<mat4>(bytes, entry, index);
            break;
        }
    }
} // namespace pe
