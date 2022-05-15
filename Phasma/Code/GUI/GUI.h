/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "imgui/imgui.h"
#include "Model/Object.h"
#include "SDL/SDL.h"

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class Context;

    class Window;

    class Surface;

    class Swapchain;

    class FrameBuffer;

    class RenderPass;

    class CommandPool;

    class CommandBuffer;

    class Fence;

    class Semaphore;

    class Image;

    class RenderPass;

    class GUI
    {
    public:
        GUI();

        ~GUI();

        // Data
        static inline float renderTargetsScale = 1.0f;
        static inline bool use_IBL = true;
        static inline bool use_Volumetric_lights = false;
        static inline int volumetric_steps = 32;
        static inline int volumetric_dither_strength = 400;
        static inline bool show_ssr = false;
        static inline bool show_ssao = false;
        static inline bool show_tonemapping = false;
        static inline float exposure = 4.5f;
        static inline bool use_AntiAliasing = false;
        static inline bool use_FXAA = false;
        static inline bool use_TAA = false;
        static inline float TAA_jitter_scale = 1.0f;
        static inline float TAA_feedback_min = 0.01f;
        static inline float TAA_feedback_max = 0.2f;
        static inline float TAA_sharp_strength = 2.0f;
        static inline float TAA_sharp_clamp = 0.35f;
        static inline float TAA_sharp_offset_bias = 1.0f;
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
        static inline bool show_motionBlur = false;
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

        bool show_demo_window = false;

        void InitImGui();

        bool render = true;
        std::string name;
        RenderPass *renderPass;
        std::vector<FrameBuffer *> framebuffers;

        void Update();

        void RenderViewPorts();

        void InitGUI(bool show = true);

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
    };
}