#pragma once

namespace pe
{
    inline constexpr const char *kGamePackFileName = "game.pepak";

    struct GamePackBuildEntry
    {
        std::string path;
        std::vector<uint8_t> data;
    };

    bool OpenGamePack(const std::filesystem::path &path, std::string *error = nullptr);
    [[nodiscard]] bool AssetFileExists(const std::filesystem::path &path); // on disk or in the game pack
    void CloseGamePack();
    [[nodiscard]] bool HasGamePack();
    [[nodiscard]] bool HasGamePackAsset(const std::filesystem::path &path);
    [[nodiscard]] bool IsGamePackManagedAsset(const std::filesystem::path &path);
    [[nodiscard]] std::optional<std::string> ReadGamePackAsset(const std::filesystem::path &path);
    [[nodiscard]] std::vector<std::string> ListGamePackAssets(const std::filesystem::path &prefix = {});
    [[nodiscard]] bool WriteGamePack(const std::filesystem::path &path,
                                     const std::vector<GamePackBuildEntry> &entries,
                                     std::string *error = nullptr);
} // namespace pe
