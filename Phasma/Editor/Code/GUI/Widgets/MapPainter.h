#pragma once
#include "GUI/Widget.h"
#include <array>

namespace pe
{
    class Image;
    class NodeVoxelWorldTag;

    // In-editor painter for the Voxel World node's MapGen input maps (surface height / strata
    // thickness / features). Edits a CPU grayscale buffer with a round falloff brush (or sparse
    // feature stamps), previews it as a texture, saves it as a PNG under Assets, and triggers a
    // world rebuild through the same rebuildRequested path as the inspector's "Rebuild World"
    // button.
    class MapPainter : public Widget
    {
    public:
        enum class Brush
        {
            Raise = 0, // add strength (Shift subtracts)
            Smooth,    // blend toward the 3x3 neighborhood average
            Flatten,   // blend toward the value under the stroke start
            Set,       // blend toward an explicit value
        };
        // Feature stamps for the Features layer. Tree/Rock/Olive/Cypress scatter a structural
        // feature id (1/2/4/5); Block solid-paints the selected block onto the surface (map value
        // kBlockPaintBase + blockId); Erase clears.
        enum class FeatureStamp
        {
            Tree = 0,
            Rock,
            Olive,
            Cypress,
            Block,
            Erase,
        };

        MapPainter();
        ~MapPainter() override;
        void Update() override;

        // Programmatic route (invoke_editor_action voxelpainter.*) so agents can paint without
        // mouse input. u/v are 0..1 across the map; radius/strength <= 0 and brush/value < 0 use
        // the widget settings. On the Features layer, brush selects the FeatureStamp instead.
        bool SetLayer(int layer);
        bool Stroke(float u, float v, float radius, float strength, bool lower, int brush = -1, int value = -1);
        bool Save();

    private:
        struct LayerBuffer
        {
            std::string loadedPath; // resolved absolute path; empty = nothing loaded
            int w = 0;
            int h = 0;
            std::vector<uint8_t> px;
            bool unsaved = false;
        };

        static constexpr int kFeaturesLayer = 3;

        NodeVoxelWorldTag *Tag() const;
        std::string &LayerPath(NodeVoxelWorldTag *tag, int layer) const;
        LayerBuffer &Buf() { return m_layers[m_layer]; }
        bool OnFeatures() const { return m_layer == kFeaturesLayer; }
        void SyncLayer(NodeVoxelWorldTag *tag, int layer); // (re)load a buffer when its tag path changed
        void CreateMap(NodeVoxelWorldTag *tag);            // allocate + save a fresh map, point the tag at it
        void ResizeMap();                                  // resample the loaded buffer to m_newW x m_newH
        void StampBrush(float px, float py, float radius, float strength, bool lower, Brush brush, int value);
        void StampFeatures(float px, float py, float radius, FeatureStamp stamp);
        void UploadPreview();
        void ReleasePreview();
        void LoadPalette(); // lazily load the block tile thumbnails for the Block picker
        void ReleasePalette();
        bool DrawPalette();                        // the tile grid; returns true when the selection changed
        void TeleportCameraTo(float px, float py); // Alt+LMB: move the camera over this map pixel (keeps height)

        int m_layer = 0; // 0 = surface height, 1 = strata 1, 2 = strata 2, 3 = features
        LayerBuffer m_layers[4];
        bool m_textureDirty = false;
        bool m_haveLastStamp = false;
        bool m_panning = false; // right-drag pans the canvas
        float m_panX = 0.0f;    // image top-left offset from the canvas origin (screen px)
        float m_panY = 0.0f;
        float m_lastPx = 0.0f;
        float m_lastPy = 0.0f;
        float m_brushRadius = 8.0f;    // map pixels
        float m_brushStrength = 12.0f; // value delta per stamp at the brush center
        int m_brushType = 0;           // Brush enum, or FeatureStamp on the Features layer
        int m_setValue = 128;          // Set brush target
        int m_flattenTarget = 64;      // sampled under the stroke start
        int m_featureSpacing = 5;      // min pixels between scattered features
        int m_paintBlock = 3;          // block id the Block stamp paints onto the surface
        // One block-tile thumbnail per palette entry (loaded lazily), + its average color for the
        // features preview.
        struct Thumb
        {
            Image *image = nullptr;
            void *tex = nullptr;
            uint8_t r = 128, g = 128, b = 128;
        };
        std::vector<Thumb> m_palette;
        float m_zoom = 1.0f;
        int m_newW = 128; // create/resize target size
        int m_newH = 128;
        int m_newValue = 64;
        std::array<char, 260> m_newPath{};
        Image *m_image = nullptr;
        void *m_textureId = nullptr;
    };
} // namespace pe
