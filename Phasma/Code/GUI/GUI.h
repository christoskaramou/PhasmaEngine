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
    class Queue;
    class RendererSystem;

    class GUI
    {
    public:
        GUI();

        ~GUI();

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