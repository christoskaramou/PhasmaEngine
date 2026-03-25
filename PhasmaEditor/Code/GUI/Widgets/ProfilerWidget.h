#pragma once
#include "GUI/Widget.h"
#include "API/RHI.h"
#include "TextEditor.h"

namespace pe
{
    class Image;

    class ProfilerWidget : public Widget
    {
    public:
        ProfilerWidget() : Widget("Profiler") { m_open = false; }
        ~ProfilerWidget();
        bool IsOpen() override { return m_open || m_waitingForCapture; }
        void Update() override;
        std::string TakeSnapshot();

    private:
        // Tab draw helpers
        void DrawOverviewTab();
        void DrawGpuTab();
        void DrawGpuTimingTable(const std::vector<GpuTimerSample> &samples,
                                float totalMs, const char *filter, int &selectedPass);
        void DrawTimelineTab();
        void DrawCpuTab();
        void DrawCpuTimelineView();
        void DrawShadersTab();
        void DrawCaptureTab();

        // Shader editor helpers
        void LoadShaderFile(const std::string &path);
        void SaveAndRecompile();
        void ScanShaderFiles();

        // Render target preview helpers
        void DrawPassImageTooltip(const std::string &passName);
        void *GetRTDescriptor(Image *image);

        // All display data — replaced atomically each tick; frozen when paused
        struct ProfilerData
        {
            float fps = 0.0f;
            float frameMs = 0.0f;
            float cpuTotalMs = 0.0f;
            float cpuUpdateMs = 0.0f;
            float cpuDrawMs = 0.0f;
            std::vector<float> frameTimeVec;
            float frameTimeMax = 5.0f;
            std::vector<GpuTimerSample> gpuSamples;
            float gpuTotal = 0.0f;
            std::vector<Profiler::Entry> cpuEntries;
            float cpuScopeTotal = 0.0f;
            SystemProcMem ram;
            GpuMemorySnapshot gpu;
        };
        ProfilerData m_data;

        // GPU UI state
        int m_selectedGpuPass = -1;
        char m_gpuFilter[64] = {};
        int m_gpuViewMode = 0; // 0 = Table, 1 = Timeline

        // CPU UI state
        int m_cpuViewMode = 0; // 0 = Table, 1 = Timeline
        char m_cpuFilter[64] = {};
        float m_cpuTimelineZoom = 1.0f;
        float m_cpuTimelineZoomV = 1.0f;
        int m_selectedCpuScope = -1;

        // Shader viewer / editor
        std::vector<std::string> m_shaderFiles;    // absolute paths
        std::vector<std::string> m_shaderRelPaths; // display paths (relative)
        int m_selectedShader = -1;
        std::string m_shaderOriginalSource;
        TextEditor m_editor;
        bool m_shaderModified = false;
        char m_shaderSearchFilter[128] = {};
        int m_editorPalette = 4;     // 0=Dark, 1=Light, 2=Retro, 3=Soft, 4=VSCode
        int m_editorFontSizeIdx = 1; // 0=S, 1=M, 2=L, 3=XL
        float m_editorFontScale = 1.0f;
        bool m_shaderFilesScanned = false;

        // Timeline
        float m_timelineZoom = 1.0f;
        float m_timelineZoomV = 1.0f;
        bool m_paused = false;

        // Capture flow
        bool m_waitingForCapture = false;
        uint32_t m_captureCountBefore = 0;

        // Update interval
        Timer m_delay;
        bool m_firstFrame = true;

        // RT descriptor cache for hover image previews (Image* → VkDescriptorSet as void*)
        std::unordered_map<Image *, void *> m_rtDescriptorCache;
        Image *m_viewportRTSnapshot = nullptr; // detect render-target recreation on resize
    };
} // namespace pe
