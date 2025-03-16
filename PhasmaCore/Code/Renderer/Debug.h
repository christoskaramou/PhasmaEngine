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
        static void Init(InstanceApiHandle instance);
        static void CreateDebugMessenger();
        static void DestroyDebugMessenger();
        template <class HANDLE>
        static void SetObjectName(const HANDLE &handle, const std::string &name)
        {
            ValidateBaseClass<ApiHandleBase, HANDLE>();
            SetObjectName(handle, HANDLE::ObType, name);
        }
        static void InitCaptureApi();
        static void DestroyCaptureApi();
        static void StartFrameCapture();
        static void EndFrameCapture();
        static void TriggerCapture();

    private:
        friend class CommandBuffer;
        friend class Queue;

        static void SetObjectName(uint64_t handle, ObjectType type, const std::string &name);
        static void BeginQueueRegion(Queue *queue, const std::string &name);
        static void InsertQueueLabel(Queue *queue, const std::string &name);
        static void EndQueueRegion(Queue *queue);
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name);
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name);
        static void EndCmdRegion(CommandBuffer *cmd);

        inline static InstanceApiHandle s_instance;
        inline static DebugMessengerApiHandle s_debugMessenger;
    };
#else
    class Debug
    {
    public:
        static void Init(InstanceApiHandle instance) {}
        static void CreateDebugMessenger() {}
        static void DestroyDebugMessenger() {}
        static void InitCaptureApi() {}
        static void DestroyCaptureApi() {}
        template <class HANDLE>
        static void SetObjectName(const HANDLE &handle, const std::string &name) {}
        static void StartFrameCapture() {}
        static void EndFrameCapture() {}
        static void TriggerCapture() {}

    private:
        friend class CommandBuffer;
        friend class Queue;

        static void SetObjectName(uint64_t object, ObjectType type, const std::string &name) {}
        static void BeginQueueRegion(Queue *queue, const std::string &name) {}
        static void InsertQueueLabel(Queue *queue, const std::string &name) {}
        static void EndQueueRegion(Queue *queue) {}
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name) {}
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name) {}
        static void EndCmdRegion(CommandBuffer *cmd) {}
    };
#endif
};
