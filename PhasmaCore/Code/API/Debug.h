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
        static void Init(vk::Instance instance);
        static void CreateDebugMessenger();
        static void DestroyDebugMessenger();
        template <class HANDLE>
        static void SetObjectName(HANDLE handle, const std::string &name)
        {
            if (!handle || name.empty())
                return;

            using Native = HANDLE::NativeType;
            const uint64_t handle64 = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<Native>(handle)));

            VkDebugUtilsObjectNameInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            info.pNext = nullptr;
            info.objectType = static_cast<VkObjectType>(HANDLE::objectType);
            info.objectHandle = handle64;
            info.pObjectName = name.c_str();
            SetObjectName(info);
        }
        static void InitCaptureApi();
        static void DestroyCaptureApi();
        static void StartFrameCapture();
        static void EndFrameCapture();
        static void TriggerCapture();

    private:
        friend class CommandBuffer;
        friend class Queue;

        static void SetObjectName(const VkDebugUtilsObjectNameInfoEXT &info);
        static void BeginQueueRegion(Queue *queue, const std::string &name);
        static void InsertQueueLabel(Queue *queue, const std::string &name);
        static void EndQueueRegion(Queue *queue);
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name);
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name);
        static void EndCmdRegion(CommandBuffer *cmd);

        inline static vk::Instance s_instance;
        inline static VkDebugUtilsMessengerEXT s_debugMessenger;
    };
#else
    class Debug
    {
    public:
        static void Init(vk::Instance instance) {}
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

        static void SetObjectName(const VkDebugUtilsObjectNameInfoEXT &info) {}
        static void BeginQueueRegion(Queue *queue, const std::string &name) {}
        static void InsertQueueLabel(Queue *queue, const std::string &name) {}
        static void EndQueueRegion(Queue *queue) {}
        static void BeginCmdRegion(CommandBuffer *cmd, const std::string &name) {}
        static void InsertCmdLabel(CommandBuffer *cmd, const std::string &name) {}
        static void EndCmdRegion(CommandBuffer *cmd) {}
    };
#endif
}; // namespace pe
