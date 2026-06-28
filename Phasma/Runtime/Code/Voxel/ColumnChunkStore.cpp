#include "Voxel/ColumnChunkStore.h"

namespace pe::voxel
{
    namespace
    {
#pragma pack(push, 1)
        struct ColumnHeader
        {
            char magic[4];
            uint16_t version;
            uint16_t sectionMask;
            int32_t cx;
            int32_t cz;
        };
#pragma pack(pop)

        static_assert(sizeof(ColumnHeader) == 16, "ColumnHeader size");

        bool ReadHeader(std::ifstream &in, ColumnHeader &hdr)
        {
            in.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
            if (!in)
                return false;
            if (std::memcmp(hdr.magic, ColumnChunkStore::kMagic, 4) != 0)
                return false;
            if (hdr.version != ColumnChunkStore::kVersion)
                return false;
            return true;
        }

        bool WriteSection(std::ofstream &out, const ChunkSection &section)
        {
            const BlockId *data = section.Store().Data();
            out.write(reinterpret_cast<const char *>(data),
                      static_cast<std::streamsize>(kBlocksPerSection * sizeof(BlockId)));
            return static_cast<bool>(out);
        }

        bool ReadSection(std::ifstream &in, ChunkSection &section)
        {
            BlockId buffer[kBlocksPerSection];
            in.read(reinterpret_cast<char *>(buffer),
                    static_cast<std::streamsize>(kBlocksPerSection * sizeof(BlockId)));
            if (!in)
                return false;
            section.Store().ReplaceAll(buffer, kBlocksPerSection);
            section.SetDirty(true);
            return true;
        }
    } // namespace

    std::filesystem::path ColumnChunkStore::ResolveRoot(const std::string &configured)
    {
        if (configured.empty())
            return {};
        std::filesystem::path path(configured);
        if (path.is_absolute())
            return path.lexically_normal();
        Path::Init();
        return (std::filesystem::path(Path::Assets) / path).lexically_normal();
    }

    std::filesystem::path ColumnChunkStore::ColumnPath(const std::filesystem::path &root, ColumnCoord coord)
    {
        const std::string name = "column_" + std::to_string(coord.cx) + "_" + std::to_string(coord.cz) + ".pevcol";
        return root / "columns" / name;
    }

    bool ColumnChunkStore::TryOverlay(const std::filesystem::path &root, ChunkColumn &column)
    {
        if (root.empty())
            return false;

        const std::filesystem::path path = ColumnPath(root, column.Coord());
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;

        ColumnHeader hdr{};
        if (!ReadHeader(in, hdr))
        {
            PE_WARN("[ColumnChunkStore] Invalid column file: %s", path.generic_string().c_str());
            return false;
        }

        if (hdr.cx != column.Coord().cx || hdr.cz != column.Coord().cz)
        {
            PE_WARN("[ColumnChunkStore] Column coord mismatch in %s", path.generic_string().c_str());
            return false;
        }

        for (int si = 0; si < kSectionCount; ++si)
        {
            if ((hdr.sectionMask & (1u << si)) == 0)
                continue;
            if (!ReadSection(in, column.Section(si)))
            {
                PE_WARN("[ColumnChunkStore] Truncated column file: %s", path.generic_string().c_str());
                return false;
            }
        }
        return true;
    }

    uint16_t ColumnChunkStore::PersistedSectionMask(const std::filesystem::path &root, ColumnCoord coord)
    {
        if (root.empty())
            return 0;

        const std::filesystem::path path = ColumnPath(root, coord);
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return 0;

        ColumnHeader hdr{};
        if (!ReadHeader(in, hdr))
            return 0;
        if (hdr.cx != coord.cx || hdr.cz != coord.cz)
            return 0;
        return hdr.sectionMask;
    }

    bool ColumnChunkStore::Save(const std::filesystem::path &root, const ChunkColumn &column, uint16_t touchedMask)
    {
        if (root.empty() || touchedMask == 0)
            return false;

        const std::filesystem::path path = ColumnPath(root, column.Coord());
        const uint16_t persistedMask = PersistedSectionMask(root, column.Coord());
        uint16_t sectionMask = touchedMask | persistedMask;
        sectionMask &= static_cast<uint16_t>((1u << kSectionCount) - 1u);

        uint16_t writeMask = 0;
        for (int si = 0; si < kSectionCount; ++si)
        {
            if ((sectionMask & (1u << si)) == 0)
                continue;
            if (column.Section(si).IsEmpty() && (touchedMask & (1u << si)) == 0)
                continue;
            writeMask |= static_cast<uint16_t>(1u << si);
        }
        if (writeMask == 0)
        {
            std::error_code ec;
            if (persistedMask != 0)
                std::filesystem::remove(path, ec);
            return persistedMask != 0;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            PE_WARN("[ColumnChunkStore] Failed to open for write: %s", path.generic_string().c_str());
            return false;
        }

        ColumnHeader hdr{};
        std::memcpy(hdr.magic, kMagic, 4);
        hdr.version = kVersion;
        hdr.sectionMask = writeMask;
        hdr.cx = column.Coord().cx;
        hdr.cz = column.Coord().cz;
        out.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
        if (!out)
            return false;

        for (int si = 0; si < kSectionCount; ++si)
        {
            if ((writeMask & (1u << si)) == 0)
                continue;
            if (!WriteSection(out, column.Section(si)))
            {
                PE_WARN("[ColumnChunkStore] Failed writing section %d to %s", si, path.generic_string().c_str());
                return false;
            }
        }
        return true;
    }
} // namespace pe::voxel
