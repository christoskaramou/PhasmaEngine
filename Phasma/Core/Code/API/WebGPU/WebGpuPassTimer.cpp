#include "API/WebGPU/WebGpuPassTimer.h"
#include "API/Debug.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "Base/EventSystem.h"
#include "Base/Timer.h"

namespace pe
{
    struct WebGpuPassTimer::Results
    {
        WGPUBuffer buffer = nullptr;
        std::vector<std::string> names;
    };

    namespace
    {
        constexpr uint64_t kPassBytes = 2 * sizeof(uint64_t); // a pass's start and end, in nanoseconds

        // Mappable buffers that are not waiting for results, shared by every recording.
        // ponytail: never released; they live as long as the page's device does.
        std::vector<WGPUBuffer> s_idleBuffers;

        WGPUBuffer CreateBuffer(WGPUBufferUsage usage, uint64_t size)
        {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.usage = usage;
            desc.size = size;
            return wgpuDeviceCreateBuffer(WebGpuRhiImpl::Get()->m_device, &desc);
        }

        std::vector<GpuTimerSample> ToSamples(const uint64_t *stamps, const std::vector<std::string> &names)
        {
            uint64_t first = UINT64_MAX;
            for (size_t i = 0; i < names.size(); ++i)
                first = std::min(first, stamps[2 * i]);

            std::vector<GpuTimerSample> samples;
            samples.reserve(names.size());
            for (size_t i = 0; i < names.size(); ++i)
            {
                const uint64_t start = stamps[2 * i];
                const uint64_t end = stamps[2 * i + 1];
                GpuTimerSample sample{};
                sample.name = names[i];
                sample.depth = 1;                                                             // the other backends keep depth 0 for the whole command buffer
                sample.timeMs = end > start ? static_cast<float>(end - start) * 1e-6f : 0.0f; // not always monotonic
                sample.startOffsetMs = static_cast<float>(start - first) * 1e-6f;
                samples.push_back(std::move(sample));
            }

            return samples;
        }
    } // namespace

    WebGpuPassTimer::~WebGpuPassTimer()
    {
        Reset();
        if (m_set)
            wgpuQuerySetRelease(m_set);

        if (m_resolved)
            wgpuBufferRelease(m_resolved);
    }

    const WGPUPassTimestampWrites *WebGpuPassTimer::Add(const std::string &name)
    {
        const bool timed = Debug::IsGpuTimingEnabled() && WebGpuRhiImpl::Get()->m_timestampQuery;
        if (!timed || m_results || m_names.size() == kMaxPasses)
            return nullptr;

        if (!m_set)
        {
            WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            desc.type = WGPUQueryType_Timestamp;
            desc.count = 2 * kMaxPasses;
            m_set = wgpuDeviceCreateQuerySet(WebGpuRhiImpl::Get()->m_device, &desc);
            m_resolved = CreateBuffer(WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc, kMaxPasses * kPassBytes);
        }

        const uint32_t pass = static_cast<uint32_t>(m_names.size());
        m_writes.querySet = m_set;
        m_writes.beginningOfPassWriteIndex = 2 * pass;
        m_writes.endOfPassWriteIndex = 2 * pass + 1;
        m_names.push_back(name);
        return &m_writes;
    }

    void WebGpuPassTimer::Resolve(WGPUCommandEncoder encoder)
    {
        if (m_names.empty())
            return;

        m_results = new Results{};
        m_results->names = std::move(m_names);
        m_names.clear();
        if (s_idleBuffers.empty())
        {
            m_results->buffer = CreateBuffer(WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, kMaxPasses * kPassBytes);
        }
        else
        {
            m_results->buffer = s_idleBuffers.back();
            s_idleBuffers.pop_back();
        }

        const uint32_t passes = static_cast<uint32_t>(m_results->names.size());
        wgpuCommandEncoderResolveQuerySet(encoder, m_set, 0, 2 * passes, m_resolved, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, m_resolved, 0, m_results->buffer, 0, passes * kPassBytes);
    }

    void WebGpuPassTimer::ReadBack()
    {
        if (!m_results)
            return;

        WGPUBufferMapCallbackInfo info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.userdata1 = m_results;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *user, void *)
        {
            std::unique_ptr<Results> results(static_cast<Results *>(user));
            if (status != WGPUMapAsyncStatus_Success)
            {
                wgpuBufferRelease(results->buffer); // the device is gone or the map was cancelled
                return;
            }

            const size_t bytes = results->names.size() * kPassBytes;
            const void *stamps = wgpuBufferGetConstMappedRange(results->buffer, 0, bytes);
            std::vector<GpuTimerSample> samples;
            if (stamps)
                samples = ToSamples(static_cast<const uint64_t *>(stamps), results->names);

            wgpuBufferUnmap(results->buffer);
            s_idleBuffers.push_back(results->buffer);
            if (!samples.empty())
                EventSystem::DispatchEvent(EventType::AfterCommandWait, std::any{std::move(samples)});
        };

        wgpuBufferMapAsync(m_results->buffer, WGPUMapMode_Read, 0, m_results->names.size() * kPassBytes, info);
        m_results = nullptr;
    }

    void WebGpuPassTimer::Reset()
    {
        m_names.clear();
        if (!m_results)
            return;

        s_idleBuffers.push_back(m_results->buffer);
        delete m_results;
        m_results = nullptr;
    }
} // namespace pe
