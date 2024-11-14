#pragma once

struct ImGuiStyle;

namespace pe
{
    constexpr float TITLEBAR_HEIGHT = 19.f;

    class CommandBuffer;
    class Queue;

    class GUI
    {
    public:
        GUI();
        ~GUI();

        void Init();
        void Update();
        void Draw(CommandBuffer * cmd);
    
    private:
        static void async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle, const std::vector<const char *> &filter);
        static void async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message);

        void Menu();
        void Loading();
        void Metrics();
        void Shaders();
        void Properties();

        float GetQueueTotalTime(Queue *queue);
        void ShowQueueGpuTimings(Queue *queue, float maxTime);

        bool show_demo_window = false;
        std::unique_ptr<Attachment> m_attachment;

    public:
        bool render = true;
    };
}
