#pragma once
#include "GUI/Widget.h"
#include <array>

namespace pe
{
    class Image;

    class SpriteEditor : public Widget
    {
    public:
        SpriteEditor();
        ~SpriteEditor() override;
        void Update() override;

    private:
        struct SpriteFrame
        {
            std::string name;
            int x = 0;
            int y = 0;
            int w = 32;
            int h = 32;
            float pivotX = 0.5f;
            float pivotY = 0.5f;
            float duration = 0.1f;
            bool hitboxEnabled = false;
            int hitboxX = 0;
            int hitboxY = 0;
            int hitboxW = 32;
            int hitboxH = 32;
        };

        struct SpriteClip
        {
            std::string name;
            int start = 0;
            int end = 0;
            float fps = 10.0f;
            bool loop = true;
        };

        struct SheetImage
        {
            std::filesystem::path path;
            std::string label;
        };

        void DrawSourcePanel();
        void DrawFramePanel();
        void DrawClipPanel();
        void DrawAtlasPanel();
        void DrawAnimationPreview();
        void DrawSheetImagesEditor();

        void OpenSourceDialog();
        void OpenCreateSheetDialog();
        void OpenAddSheetImagesDialog();
        void OpenSaveSheetDialog(std::vector<std::filesystem::path> images);
        bool HandleSourceDropTarget();
        bool LoadSourceAsset(const std::filesystem::path &path);
        void LoadImageFromBuffer();
        void LoadImage(const std::filesystem::path &path);
        bool LoadSheetAsset(const std::filesystem::path &path, int preferredImageIndex = 0);
        void SaveSheetAsset();
        bool SaveSheetAssetAs(const std::filesystem::path &path, const std::vector<SheetImage> &images);
        bool SelectSheetImage(int index);
        void SetSheetPath(const std::filesystem::path &path);
        void ReleaseImage();
        void GenerateGridFrames();
        void ClampSelection();
        void ClampFrame(SpriteFrame &frame) const;
        void UpdatePlayback();
        void SaveMetadata();
        void LoadMetadataFromBuffer();
        void LoadMetadata(const std::filesystem::path &path);
        void SetImagePath(const std::filesystem::path &path);
        void SetMetadataPath(const std::filesystem::path &path);
        void SuggestMetadataPath();

        std::filesystem::path ResolveAssetPath(const char *path) const;
        std::filesystem::path ResolveSheetEntryPath(const std::string &path, const std::filesystem::path &sheetDir) const;
        std::string MakeAssetRelative(const std::filesystem::path &path) const;
        static void CopyToBuffer(std::array<char, 512> &buffer, const std::string &value);
        static std::string BufferString(const std::array<char, 512> &buffer);
        static bool HasImageExtension(const std::filesystem::path &path);
        static bool IsSheetAssetFile(const std::filesystem::path &path);
        static std::vector<std::string> SourcePickerExtensions();
        static std::string PathToUtf8(const std::filesystem::path &path);

        std::array<char, 512> m_imagePath{};
        std::array<char, 512> m_sheetPath{};
        std::array<char, 512> m_metadataPath{};
        std::string m_status;

        Image *m_image = nullptr;
        void *m_textureId = nullptr;
        std::filesystem::path m_loadedImagePath;
        std::filesystem::path m_loadedSheetPath;
        std::vector<SheetImage> m_sheetImages;
        int m_selectedSheetImage = -1;

        int m_gridFrameWidth = 32;
        int m_gridFrameHeight = 32;
        int m_gridMarginX = 0;
        int m_gridMarginY = 0;
        int m_gridSpacingX = 0;
        int m_gridSpacingY = 0;
        int m_gridMaxFrames = 0;

        std::vector<SpriteFrame> m_frames;
        std::vector<SpriteClip> m_clips;
        int m_selectedFrame = -1;
        int m_selectedClip = 0;
        bool m_playing = false;
        bool m_interpolate = false;
        int m_previewFrame = -1;
        float m_playAccumulator = 0.0f;
        float m_atlasZoom = 1.0f;
    };
} // namespace pe
