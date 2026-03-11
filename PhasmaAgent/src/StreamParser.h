#pragma once

#include "PhasmaAgent/Agent.h"
#include <string>
#include <vector>

namespace pagent
{
    class StreamParser
    {
    public:
        explicit StreamParser(IProviderBackend *backend);
        void Reset(IProviderBackend *backend);
        bool Feed(const char *data, std::size_t len, std::vector<AgentEvent> &out_events);

    private:
        bool ProcessLine(const std::string &line, std::vector<AgentEvent> &out_events);

        IProviderBackend *m_backend = nullptr;
        std::string m_lineBuffer; // partial line accumulator
        std::string m_eventData;  // accumulates data: payloads between blank lines
        bool m_done = false;
    };
} // namespace pagent
