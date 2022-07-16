#include "Code/App/App.h"
#include "Window/Window.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"

namespace pe
{
    App::App()
    {
        // freopen("log.txt", "w", stdout);
        // freopen("errors.txt", "w", stderr);

        EventSystem::Init();

        uint32_t flags = SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#if PE_VULKAN
        flags |= SDL_WINDOW_VULKAN;
#endif
        window = Window::Create(50, 50, 1280, 720, flags);
        RHII.Init(window->Handle());

        Queue *queue = Queue::GetNext(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());

        cmd->Begin();
        CONTEXT->CreateSystem<CameraSystem>()->Init(cmd);
        CONTEXT->CreateSystem<LightSystem>()->Init(cmd);
        CONTEXT->CreateSystem<RendererSystem>()->Init(cmd);
        CONTEXT->CreateSystem<PostProcessSystem>()->Init(cmd);
        cmd->End();

        PipelineStageFlags stages[1]{PipelineStage::AllCommandsBit};
        queue->Submit(1, &cmd, stages, 0, nullptr, 0, nullptr);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();
        Queue::Return(queue);

        FileWatcher::Start(0.25);
        frameTimer = &FrameTimer::Instance();
        context = CONTEXT;
    }

    App::~App()
    {
        FileWatcher::Clear();
        FileWatcher::Stop();
        context->DestroySystems();
        RHII.Destroy();
        RHII.Remove();
        Window::Destroy(window);
        EventSystem::Destroy();
    }

    void App::Run()
    {
        while (true)
        {
            frameTimer->Start();

            if (!window->ProcessEvents(frameTimer->GetDelta()))
                break;

            if (!window->isMinimized())
            {
                context->UpdateSystems(frameTimer->GetDelta());
                SyncQueue<Launch::All>::ExecuteRequests();
                frameTimer->cpuStamp = static_cast<float>(frameTimer->Count());
                context->DrawSystems();
            }

            frameTimer->ThreadSleep(1.0 / static_cast<double>(GUI::fps) - frameTimer->Count());
            frameTimer->Tick();

            RHII.NextFrame();
        }
    }
}