#include "Project/GamePack.h"

#include <array>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace pe
{
    namespace
    {
        constexpr std::array<char, 8> kMagic = {'P', 'E', 'P', 'A', 'K', '0', '1', '\0'};
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kMaxEntries = 100000;
        constexpr uint32_t kMaxPathLength = 4096;

        struct PackEntry
        {
            uint64_t offset = 0;
            uint64_t size = 0;
        };

        std::filesystem::path s_packPath;
        std::unordered_map<std::string, PackEntry> s_entries;
        std::unordered_set<std::string> s_managedRoots;

        void SetError(std::string *error, std::string message)
        {
            if (error)
                *error = std::move(message);
        }

        std::string NormalizeRelativePath(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.starts_with("./"))
                path.erase(0, 2);
            if (path.starts_with("Assets/"))
                path.erase(0, 7);

            const std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
            if (normalized.empty() || normalized.is_absolute())
                return {};

            for (const auto &part : normalized)
            {
                if (part == "..")
                    return {};
            }
            return normalized.generic_string();
        }

        std::string ToPackPath(const std::filesystem::path &path)
        {
            if (path.empty())
                return {};

            std::filesystem::path relative = path.lexically_normal();
            if (relative.is_absolute())
            {
                Path::Init();
                const std::filesystem::path assets = std::filesystem::path(Path::Assets).lexically_normal();
                relative = relative.lexically_relative(assets);
                std::string normalized = NormalizeRelativePath(relative.generic_string());
                if (!normalized.empty())
                    return normalized;

                const std::filesystem::path runtimeAssets = std::filesystem::path(Path::RuntimeAssets).lexically_normal();
                relative = path.lexically_normal().lexically_relative(runtimeAssets);
                normalized = NormalizeRelativePath(relative.generic_string());
                return normalized.empty() ? std::string{} : "RuntimeAssets/" + normalized;
            }
            return NormalizeRelativePath(relative.generic_string());
        }

        uint64_t HashBytes(const uint8_t *data, size_t size, uint64_t hash = 14695981039346656037ull)
        {
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= data[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template <typename T>
        bool ReadValue(std::ifstream &file, T &value)
        {
            return static_cast<bool>(file.read(reinterpret_cast<char *>(&value), sizeof(value)));
        }

        template <typename T>
        void WriteValue(std::ofstream &file, const T &value)
        {
            file.write(reinterpret_cast<const char *>(&value), sizeof(value));
        }

        bool VerifyData(std::ifstream &file, uint64_t size, uint64_t expected)
        {
            std::array<uint8_t, 65536> buffer{};
            uint64_t remaining = size;
            uint64_t hash = 14695981039346656037ull;
            while (remaining > 0)
            {
                const size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
                if (!file.read(reinterpret_cast<char *>(buffer.data()), chunk))
                    return false;
                hash = HashBytes(buffer.data(), chunk, hash);
                remaining -= chunk;
            }
            return hash == expected;
        }
    } // namespace

    bool OpenGamePack(const std::filesystem::path &path, std::string *error)
    {
        CloseGamePack();

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            SetError(error, "Failed to open game pack: " + path.generic_string());
            return false;
        }

        const std::streamoff end = file.tellg();
        if (end < static_cast<std::streamoff>(kMagic.size() + sizeof(uint32_t) * 2))
        {
            SetError(error, "Game pack is truncated: " + path.generic_string());
            return false;
        }
        const uint64_t fileSize = static_cast<uint64_t>(end);
        file.seekg(0);

        std::array<char, 8> magic{};
        uint32_t version = 0;
        uint32_t count = 0;
        if (!file.read(magic.data(), magic.size()) || !ReadValue(file, version) || !ReadValue(file, count) ||
            magic != kMagic || version != kVersion || count > kMaxEntries)
        {
            SetError(error, "Invalid or unsupported game pack: " + path.generic_string());
            return false;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t pathLength = 0;
            uint64_t dataSize = 0;
            uint64_t expectedHash = 0;
            if (!ReadValue(file, pathLength) || !ReadValue(file, dataSize) || !ReadValue(file, expectedHash) ||
                pathLength == 0 || pathLength > kMaxPathLength)
            {
                SetError(error, "Invalid game pack entry header");
                CloseGamePack();
                return false;
            }

            std::string entryPath(pathLength, '\0');
            if (!file.read(entryPath.data(), pathLength))
            {
                SetError(error, "Truncated game pack path");
                CloseGamePack();
                return false;
            }
            entryPath = NormalizeRelativePath(std::move(entryPath));
            if (entryPath.empty() || s_entries.contains(entryPath))
            {
                SetError(error, "Invalid or duplicate game pack path");
                CloseGamePack();
                return false;
            }

            const std::streamoff dataOffset = file.tellg();
            if (dataOffset < 0 || static_cast<uint64_t>(dataOffset) > fileSize ||
                dataSize > fileSize - static_cast<uint64_t>(dataOffset) ||
                !VerifyData(file, dataSize, expectedHash))
            {
                SetError(error, "Corrupt game pack entry: " + entryPath);
                CloseGamePack();
                return false;
            }

            s_entries.emplace(entryPath, PackEntry{static_cast<uint64_t>(dataOffset), dataSize});
            const size_t slash = entryPath.find('/');
            s_managedRoots.insert(entryPath.substr(0, slash));
        }

        if (static_cast<uint64_t>(file.tellg()) != fileSize)
        {
            SetError(error, "Unexpected trailing data in game pack");
            CloseGamePack();
            return false;
        }

        s_packPath = std::filesystem::absolute(path).lexically_normal();
        return true;
    }

    void CloseGamePack()
    {
        s_packPath.clear();
        s_entries.clear();
        s_managedRoots.clear();
    }

    bool HasGamePack()
    {
        return !s_packPath.empty();
    }

    bool HasGamePackAsset(const std::filesystem::path &path)
    {
        return s_entries.contains(ToPackPath(path));
    }

    bool IsGamePackManagedAsset(const std::filesystem::path &path)
    {
        if (!HasGamePack())
            return false;
        const std::string relative = ToPackPath(path);
        if (relative.empty())
            return false;
        const size_t slash = relative.find('/');
        return s_managedRoots.contains(relative.substr(0, slash));
    }

    std::optional<std::string> ReadGamePackAsset(const std::filesystem::path &path)
    {
        const std::string relative = ToPackPath(path);
        const auto it = s_entries.find(relative);
        if (it == s_entries.end())
            return std::nullopt;

        std::ifstream file(s_packPath, std::ios::binary);
        if (!file || it->second.size > std::numeric_limits<size_t>::max())
            return std::nullopt;
        file.seekg(static_cast<std::streamoff>(it->second.offset));
        std::string data(static_cast<size_t>(it->second.size), '\0');
        if (!file.read(data.data(), static_cast<std::streamsize>(data.size())))
            return std::nullopt;
        return data;
    }

    std::vector<std::string> ListGamePackAssets(const std::filesystem::path &prefix)
    {
        std::vector<std::string> paths;
        bool projectRoot = false;
        if (!prefix.empty() && prefix.is_absolute())
        {
            Path::Init();
            projectRoot = prefix.lexically_normal() == std::filesystem::path(Path::Assets).lexically_normal();
        }
        std::string normalizedPrefix = prefix.empty() || projectRoot ? std::string{} : ToPackPath(prefix);
        if (!normalizedPrefix.empty() && normalizedPrefix.back() != '/')
            normalizedPrefix.push_back('/');

        for (const auto &[path, entry] : s_entries)
        {
            (void)entry;
            if (projectRoot && path.starts_with("RuntimeAssets/"))
                continue;
            if (normalizedPrefix.empty() || path.starts_with(normalizedPrefix))
                paths.push_back(path);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    bool WriteGamePack(const std::filesystem::path &path,
                       const std::vector<GamePackBuildEntry> &entries,
                       std::string *error)
    {
        if (entries.size() > kMaxEntries)
        {
            SetError(error, "Too many game pack entries");
            return false;
        }

        std::filesystem::path temporary = path;
        temporary += ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            SetError(error, "Failed to create game pack directory: " + ec.message());
            return false;
        }
        std::filesystem::remove(temporary, ec);

        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            SetError(error, "Failed to create game pack: " + temporary.generic_string());
            return false;
        }

        file.write(kMagic.data(), kMagic.size());
        WriteValue(file, kVersion);
        const uint32_t count = static_cast<uint32_t>(entries.size());
        WriteValue(file, count);

        std::unordered_set<std::string> paths;
        for (const GamePackBuildEntry &entry : entries)
        {
            const std::string normalized = NormalizeRelativePath(entry.path);
            if (normalized.empty() || normalized.size() > kMaxPathLength || !paths.insert(normalized).second)
            {
                file.close();
                std::filesystem::remove(temporary, ec);
                SetError(error, "Invalid or duplicate game pack path: " + entry.path);
                return false;
            }

            const uint32_t pathLength = static_cast<uint32_t>(normalized.size());
            const uint64_t dataSize = static_cast<uint64_t>(entry.data.size());
            const uint64_t hash = HashBytes(entry.data.data(), entry.data.size());
            WriteValue(file, pathLength);
            WriteValue(file, dataSize);
            WriteValue(file, hash);
            file.write(normalized.data(), normalized.size());
            file.write(reinterpret_cast<const char *>(entry.data.data()),
                       static_cast<std::streamsize>(entry.data.size()));
        }
        file.close();
        if (!file)
        {
            std::filesystem::remove(temporary, ec);
            SetError(error, "Failed while writing game pack");
            return false;
        }

        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec)
        {
            std::filesystem::remove(temporary, ec);
            SetError(error, "Failed to finalize game pack: " + ec.message());
            return false;
        }
        return true;
    }
} // namespace pe
