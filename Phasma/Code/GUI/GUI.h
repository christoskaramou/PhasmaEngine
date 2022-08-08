#pragma once

#include "imgui/imgui.h"
#include "SDL/SDL.h"

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class Window;
    class FrameBuffer;
    class RenderPass;
    class CommandBuffer;
    class Image;

    class GUI
    {
    public:
        GUI();

        ~GUI();

        // Data
        static inline float renderTargetsScale = 0.5f;
        static inline bool use_IBL = true;
        static inline bool use_Volumetric_lights = false;
        static inline int volumetric_steps = 32;
        static inline int volumetric_dither_strength = 400;
        static inline bool show_ssr = false;
        static inline bool show_ssao = true;
        static inline bool show_tonemapping = false;
        static inline float exposure = 4.5f;
        static inline bool use_FXAA = false;
        static inline bool use_FSR2 = true;
        static inline float FSR2_JitterScaleX = 1.0f;
        static inline float FSR2_JitterScaleY = 1.0f;
        static inline float FSR2_MotionScaleX = 1.0f;
        static inline float FSR2_MotionScaleY = 1.0f;
        static inline float FSR2_ProjScaleX = 1.0f;
        static inline float FSR2_ProjScaleY = 1.0f;
        static inline bool use_DOF = false;
        static inline float DOF_focus_scale = 15.0f;
        static inline float DOF_blur_range = 5.0f;
        static inline bool use_SSGI = false;
        static inline bool show_Bloom = false;
        static inline float Bloom_Inv_brightness = 20.0f;
        static inline float Bloom_intensity = 1.5f;
        static inline float Bloom_range = 2.5f;
        static inline bool use_tonemap = false;
        static inline bool use_compute = false;
        static inline float Bloom_exposure = 3.5f;
        static inline bool show_motionBlur = true;
        static inline float motionBlur_strength = 1.0f;
        static inline bool randomize_lights = false;
        static inline float lights_intensity = 10.0f;
        static inline float lights_range = 10.0f;
        static inline bool use_fog = false;
        static inline float fog_ground_thickness = 30.0f;
        static inline float fog_global_thickness = 0.3f;
        static inline float fog_max_height = 3.0f;
        static inline bool shadow_cast = true;
        static inline float sun_intensity = 7.f;
        static inline std::array<float, 3> sun_direction{0.1f, 0.9f, 0.1f};
        static inline int fps = 60;
        static inline float cameraSpeed = 3.5f;
        static inline std::array<float, 3> depthBias{0.0f, 0.0f, -6.2f};
        static inline float timeScale = 1.f;
        static inline std::vector<std::string> fileList{};
        static inline std::vector<std::string> shaderList{};
        static inline std::vector<std::string> modelList{};
        static inline std::vector<std::array<float, 3>> model_scale{};
        static inline std::vector<std::array<float, 3>> model_pos{};
        static inline std::vector<std::array<float, 3>> model_rot{};
        static inline int modelItemSelected = -1;
        static inline Image *s_currRenderImage = nullptr;
        static inline std::vector<Image *> s_renderImages{};
        static inline std::atomic_uint32_t loadingCurrent{};
        static inline std::atomic_uint32_t loadingTotal{};
        static inline bool freezeFrustumCulling = false;

        void InitGUI(bool show = true);

        void Update();

        void RenderViewPorts();

        void InitImGui();

        void Draw(CommandBuffer *cmd, uint32_t imageIndex);

        static void SetWindowStyle(ImGuiStyle *dst = nullptr);

        void UpdateWindows();

        void Menu() const;

        void Loading() const;

        void Metrics() const;

        static void ConsoleWindow();

        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle,
                                                   const std::vector<const char *> &filter);

        static void
        async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

        void Scripts() const;

        void Shaders() const;

        void Models() const;

        void Properties() const;

        void CreateRenderPass();

        void CreateFrameBuffers();

        void Destroy();

    public:
        bool show_demo_window = false;
        bool render = true;
        std::string name;
        RenderPass *renderPass;
        std::vector<FrameBufferHandle> framebuffers;
        Image *m_displayRT;
    };
}