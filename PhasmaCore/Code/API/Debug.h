#pragma once

namespace pe
{
    class Queue;
    class CommandBuffer;
    struct GpuTimerInfo;

#if PE_DEBUG_MODE
    class Debug
    {
    public:
        static void Init();
        static void CreateDebugMessenger();
        static void DestroyDebugMessenger();
        static void Destroy();
        template <class HANDLE>
        static void SetObjectName(HANDLE handle, const std::string &name)
        {
            if (!handle || name.empty())
                return;

            uint64_t handle64 = static_cast<uint64_t>(detail::ToUintPtr(handle));
            SetObjectNameRaw(static_cast<uint32_t>(HANDLE::objectType), handle64, name.c_str());
        }
        static void InitCaptureApi();
        static void DestroyCaptureApi();
        static void TriggerMultiFrameCapture(uint32_t numFrames);
        static void ShowOrLaunchReplayUI();
        static uint32_t GetNumCaptures(); // returns the total number of RenderDoc captures saved to disk since the app started
        static bool IsCaptureApiAvailable();

    private:
        friend class CommandBuffer;
        friend class Queue;
        friend struct VulkanCommandBufferImpl;
        friend struct Dx12CommandBufferImpl;

        static void SetObjectNameRaw(uint32_t objectType, uint64_t objectHandle, const char *name);
        static void BeginQueueRegion(Queue *queue, const std::string &name);
        static void InsertQueueLabel(Queue *queue, const std::string &name);
        static void EndQueueRegion(Queue *queue);
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name);
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name);
        static void EndCmdRegion(CommandBuffer *cmd);

        inline static PeBackendHandle s_instance;
        inline static PeBackendHandle s_debugMessenger;
    };
#else
    class Debug
    {
    public:
        static void Init() {}
        static void CreateDebugMessenger() {}
        static void DestroyDebugMessenger() {}
        static void Destroy() {}
        static void InitCaptureApi() {}
        static void DestroyCaptureApi() {}
        template <class HANDLE>
        static void SetObjectName(const HANDLE &handle, const std::string &name) {}
        static void TriggerMultiFrameCapture(uint32_t) {}
        static void ShowOrLaunchReplayUI() {}
        static uint32_t GetNumCaptures() { return 0; }
        static bool IsCaptureApiAvailable() { return false; }

    private:
        friend class CommandBuffer;
        friend class Queue;
        friend struct VulkanCommandBufferImpl;
        friend struct Dx12CommandBufferImpl;

        static void SetObjectNameRaw(uint32_t, uint64_t, const char *) {}
        static void BeginQueueRegion(Queue *queue, const std::string &name) {}
        static void InsertQueueLabel(Queue *queue, const std::string &name) {}
        static void EndQueueRegion(Queue *queue) {}
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name) {}
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name) {}
        static void EndCmdRegion(CommandBuffer *cmd) {}
    };
#endif
}; // namespace pe
