#include "StreamParser.h"

namespace pagent
{
    StreamParser::StreamParser(IProviderBackend *backend)
        : m_backend(backend)
    {
    }

    void StreamParser::Reset(IProviderBackend *backend)
    {
        m_backend = backend;
        m_lineBuffer.clear();
        m_eventData.clear();
        m_done = false;
        if (m_backend)
            m_backend->ResetStreamState();
    }

    bool StreamParser::Feed(const char *data, std::size_t len, std::vector<AgentEvent> &out_events)
    {
        if (m_done || !m_backend)
            return false;

        m_lineBuffer.append(data, len);

        std::size_t start = 0;
        while (true)
        {
            const std::size_t nl = m_lineBuffer.find('\n', start);
            if (nl == std::string::npos)
                break;

            std::string line = m_lineBuffer.substr(start, nl - start);
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!ProcessLine(line, out_events))
            {
                m_done = true;
                m_lineBuffer.erase(0, nl + 1);
                return false;
            }

            start = nl + 1;
        }
        m_lineBuffer.erase(0, start);
        return true;
    }

    bool StreamParser::ProcessLine(const std::string &line, std::vector<AgentEvent> &out_events)
    {
        if (line.empty())
        {
            if (!m_eventData.empty())
            {
                const bool cont = m_backend->ParseStreamEvent(m_eventData, out_events);
                m_eventData.clear();
                if (!cont)
                    return false;
            }
            return true;
        }

        if (line.rfind("data:", 0) == 0)
        {
            // "data: <payload>" or "data:<payload>"
            std::size_t offset = 5;
            if (offset < line.size() && line[offset] == ' ')
                ++offset;
            if (!m_eventData.empty())
                m_eventData += '\n';
            m_eventData += line.substr(offset);
        }

        return true;
    }
} // namespace pagent
