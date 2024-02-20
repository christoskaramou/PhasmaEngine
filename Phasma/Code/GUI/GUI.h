#pragma once

#include "imgui/imgui.h"
#include "SDL/SDL.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class Window;
    class FrameBuffer;
    class RenderPass;
    class CommandBuffer;
    class Image;
    class Queue;
    class RendererSystem;

    class GUI
    {
    public:
        GUI();

        ~GUI();

        // TODO: Move these to the corresponding class settings
        // ---------------------------------------------
        static inline float renderTargetsScale = 1.0f; //0.5f;
        static inline bool use_IBL = true;
        static inline float IBL_intensity = 0.75f;
        static inline bool use_Volumetric_lights = false;
        static inline int volumetric_steps = 32;
        static inline float volumetric_dither_strength = 400;
        static inline bool show_ssr = false;
        static inline bool show_ssao = true;
        static inline bool show_tonemapping = false;
        static inline float exposure = 4.5f;
        static inline bool use_FXAA = true;
        static inline float FSR2_MotionScaleX = 1.0f;
        static inline float FSR2_MotionScaleY = 1.0f;
        static inline float FSR2_ProjScaleX = 1.0f;
        static inline float FSR2_ProjScaleY = 1.0f;
        static inline bool use_DOF = false;
        static inline float DOF_focus_scale = 15.0f;
        static inline float DOF_blur_range = 5.0f;
        static inline bool show_Bloom = false;
        static inline float Bloom_strength = 1.0f;
        static inline float Bloom_range = 1.0f;
        static inline bool use_compute = false;
        static inline bool show_motionBlur = true;
        static inline float motionBlur_strength = 1.0f;
        static inline int motionBlur_samples = 16;
        static inline bool randomize_lights = false;
        static inline float lights_intensity = 7.0f;
        static inline float lights_range = 7.0f;
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
        static inline Image *s_currRenderImage = nullptr;
        static inline std::vector<Image *> s_renderImages{};
        static inline std::string loadingName = "Loading";
        static inline std::atomic_uint32_t loadingCurrent{};
        static inline std::atomic_uint32_t loadingTotal{};
        static inline bool freezeFrustumCulling = false;
        static inline bool drawAABBs = false;
        static inline bool aabbDepthAware = true;
        static inline int cullsPerTask = 10;
        // ---------------------------------------------

        void InitGUI();

        void Update();

        void RenderViewPorts();

        void InitImGui();

        CommandBuffer *Draw();

        static void SetWindowStyle(ImGuiStyle *dst = nullptr);

        void UpdateWindows();

        void Menu() const;

        void Loading() const;

        void Metrics() const;

        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle, const std::vector<const char *> &filter);

        static void async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

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
        std::vector<Attachment> attachments;
        RendererSystem *renderer;

        Queue *m_renderQueue;
    };
}