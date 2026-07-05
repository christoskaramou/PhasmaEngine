#pragma once
#include "GUI/Widget.h"
#include <array>

namespace pe
{
    class Image;
    class NodeVoxelWorldTag;
    struct NodeId;

    // In-editor painter for terrain input maps. The surface-height layer paints a Terrain node's
    // heightmap when one exists (else the Voxel World's); strata thickness / features are Voxel World
    // only. Edits a CPU grayscale buffer with a round falloff brush (or sparse
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
        // Viewport Terrain Brush route: scatter-stamp at a world position (radius in metres, kindId
        // 1-based, 0 = erase) and apply live. Owns the world<->map transform so it stays in one place.
        bool ScatterStrokeWorld(float worldX, float worldZ, float radiusM, int kindId);

    private:
        struct LayerBuffer
        {
            std::string loadedPath; // resolved absolute path; empty = nothing loaded
            int w = 0;
            int h = 0;
            // Values in a [0,255] domain (float, no integer snapping) so a surface map keeps the file's
            // half-float precision; strata/features hold integer-valued floats (thickness / discrete ids).
            std::vector<float> px;
            bool unsaved = false;
        };

        static constexpr int kFeaturesLayer = 3;
        static constexpr int kCavesLayer = 4;   // Terrain node only: painted underground voids
        static constexpr int kScatterLayer = 5; // Terrain node only: painted mesh scatter (kind ids)
        static constexpr int kLayerCount = 6;

        // The node fields that own a painter layer, resolved per layer: the height layer (0) prefers a
        // Terrain node when one exists; strata/features (1-3) are Voxel World only. Pointers are into the
        // owning node's live tag so the painter edits either node uniformly. path == null = no owner.
        struct MapTarget
        {
            std::string *path = nullptr;
            int *blocksPerPixel = nullptr;                     // Voxel World node: integer blocks per pixel
            float *metersPerPixel = nullptr;                   // Terrain node: float metres per pixel (one non-null)
            std::vector<std::string> *scatterMeshes = nullptr; // Terrain node: the scatter kind list
            bool *rebuild = nullptr;
            float *heightMin = nullptr; // layer-0 surface-value readout
            float *heightMax = nullptr;
            float *groundHeight = nullptr;
            NodeId *node = nullptr;   // world position = map center
            bool bounded = true;      // node position is the map center
            bool terrainFlip = false; // Terrain samples col 0 = +X, row 0 = +Z (both flip vs Voxel World)
            bool valid() const { return path != nullptr; }
            // World units each map pixel spans: metres for Terrain, blocks for the Voxel World.
            float ppScale() const
            {
                return metersPerPixel ? *metersPerPixel : (blocksPerPixel ? (float)*blocksPerPixel : 1.0f);
            }
        };
        MapTarget ResolveTarget(int layer) const;
        LayerBuffer &Buf() { return m_layers[m_layer]; }
        bool OnFeatures() const { return m_layer == kFeaturesLayer; }
        bool OnScatter() const { return m_layer == kScatterLayer; }
        void SyncLayer(int layer); // (re)load a buffer when its owning node's path changed
        void CreateMap();          // allocate + save a fresh map, point the owning node's path at it
        void ResizeMap();          // resample the loaded buffer to m_newW x m_newH
        void StampBrush(float px, float py, float radius, float strength, bool lower, Brush brush, float value);
        void StampFeatures(float px, float py, float radius, FeatureStamp stamp);
        // Scatter layer: jittered-grid dots of a 1-based kind id (0 = erase disk), like StampFeatures.
        void StampScatter(LayerBuffer &buf, float px, float py, float radius, int kindId);
        // Push the scatter buffer to the live TerrainWorld and re-mesh tiles under the stamped pixel
        // rect (no rebuild). False = no live world/templates; caller falls back to rebuildRequested.
        bool PushScatterLive(float px0, float py0, float px1, float py1);
        void UploadPreview();
        void ReleasePreview();
        void LoadPalette(); // lazily load the block tile thumbnails for the Block picker
        void ReleasePalette();
        bool DrawPalette();                        // the tile grid; returns true when the selection changed
        void TeleportCameraTo(float px, float py); // Alt+LMB: move the camera over this map pixel (keeps height)

        int m_layer = 0; // 0 = surface height, 1 = strata 1, 2 = strata 2, 3 = features, 4 = caves
        LayerBuffer m_layers[kLayerCount];
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
        float m_setValue = 127.5f;     // Set brush target ([0,255] domain; 127.5 = 0 surface scaler)
        float m_flattenTarget = 64.0f; // sampled under the stroke start
        int m_featureSpacing = 5;      // min pixels between scattered features / scatter instances
        int m_paintBlock = 3;          // block id the Block stamp paints onto the surface
        int m_scatterKind = 0;         // Scatter layer combo index: 0..N-1 = kind 1..N, N = Erase
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
        float m_newValue = 127.5f; // create fill ([0,255] domain): 127.5 = 0 scaler = Ground Height
        std::array<char, 260> m_newPath{};
        Image *m_image = nullptr;
        void *m_textureId = nullptr;
    };
} // namespace pe
