#pragma once

namespace pe
{
    class Queue;
    class CommandBuffer;

#if PE_DEBUG_MODE
    class Debug
    {
    public:
        static void Init(InstanceHandle instance);

        static void GetInstanceUtils(std::vector<const char *> &instanceExtensions,
                                     std::vector<const char *> &instanceLayers);

        static void CreateDebugMessenger();

        static void DestroyDebugMessenger();

        static void SetObjectName(uintptr_t object, ObjectType type, const std::string &name);

        static void StartFrameCapture();

        static void EndFrameCapture();

    private:
        friend class CommandBuffer;
        friend class Queue;

        static void BeginQueueRegion(Queue *queue, const std::string &name);

        static void InsertQueueLabel(Queue *queue, const std::string &name);

        static void EndQueueRegion(Queue *queue);

        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name);

        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name);

        static void EndCmdRegion(CommandBuffer *cmd);

        inline static InstanceHandle s_instance;
        inline static DebugMessengerHandle s_debugMessenger;
    };
#else
    class Debug
    {
    public:
        static void Init(InstanceHandle instance) {}

        static void GetInstanceUtils(std::vector<const char *> &instanceExtensions,
                                     std::vector<const char *> &instanceLayers) {}

        static void CreateDebugMessenger() {}

        static void DestroyDebugMessenger() {}

        static void SetObjectName(uint64_t object, ObjectType type, const std::string &name) {}

        static void StartFrameCapture() {}

        static void EndFrameCapture() {}
        
    private:
        friend class CommandBuffer;
        friend class Queue;
        
        static void BeginQueueRegion(Queue *queue, const std::string &name) {}

        static void InsertQueueLabel(Queue *queue, const std::string &name) {}

        static void EndQueueRegion(Queue *queue) {}

        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name) {}

        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name) {}

        static void EndCmdRegion(CommandBuffer *cmd) {}
    };
#endif
};