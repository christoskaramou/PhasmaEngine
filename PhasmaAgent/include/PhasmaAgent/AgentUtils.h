#pragma once

#include <string>
#include <initializer_list>
#include <utility>

namespace pagent
{
    // JSON-encode a string value (produces "...")
    inline std::string JsonStr(const std::string &s)
    {
        std::string out = "\"";
        for (char c : s)
        {
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else
                out += c;
        }
        return out + '"';
    }

    // unescape a JSON string value (handles \\ \/ \" \n \t \r)
    inline std::string JsonUnescape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                char next = s[++i];
                if (next == '\\')
                    out += '\\';
                else if (next == '/')
                    out += '/';
                else if (next == '"')
                    out += '"';
                else if (next == 'n')
                    out += '\n';
                else if (next == 't')
                    out += '\t';
                else if (next == 'r')
                    out += '\r';
                else
                {
                    out += '\\';
                    out += next;
                }
            }
            else
                out += s[i];
        }
        return out;
    }

    // build a flat JSON object from key/value pairs (values are already JSON-encoded)
    inline std::string JsonObj(std::initializer_list<std::pair<const char *, std::string>> kv)
    {
        std::string out = "{";
        bool first = true;
        for (auto &[k, v] : kv)
        {
            if (!first)
                out += ",";
            out += JsonStr(k) + ":" + v;
            first = false;
        }
        return out + "}";
    }

    // extract a JSON string value by key from a flat tool-call args object
    inline std::string ExtractArgStr(const std::string &args, const char *key)
    {
        std::string needle = std::string("\"") + key + "\"";
        auto pos = args.find(needle);
        if (pos == std::string::npos)
            return "";
        pos = args.find('"', pos + needle.size() + 1);
        if (pos == std::string::npos)
            return "";
        auto end = args.find('"', pos + 1);
        if (end == std::string::npos)
            return "";
        return args.substr(pos + 1, end - pos - 1);
    }

    // extract a JSON number value by key from a flat tool-call args object
    inline float ExtractArgNum(const std::string &args, const char *key)
    {
        std::string needle = std::string("\"") + key + "\":";
        auto pos = args.find(needle);
        if (pos == std::string::npos)
            return 0.0f;
        try
        {
            return std::stof(args.substr(pos + needle.size()));
        }
        catch (...)
        {
            return 0.0f;
        }
    }
} // namespace pagent
