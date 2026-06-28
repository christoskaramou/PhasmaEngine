#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace pmcp
{
    // JSON-encode a string value (produces "...")
    inline std::string JsonStr(const std::string &s)
    {
        nlohmann::json j = s;
        return j.dump();
    }

    // unescape a JSON string value (handles \\ \/ \" \n \t \r \uXXXX)
    inline std::string JsonUnescape(const std::string &s)
    {
        // nlohmann::json expect a full JSON value.
        // If s is just the raw content of a string (without quotes), wrap it.
        if (s.empty())
            return "";

        std::string wrapped = s;
        if (wrapped.front() != '"')
            wrapped = "\"" + wrapped + "\"";

        try
        {
            return nlohmann::json::parse(wrapped).get<std::string>();
        }
        catch (...)
        {
            return s; // Fallback to raw if parsing fails
        }
    }

    // build a flat JSON object from key/value pairs (values are already JSON-encoded)
    inline std::string JsonObj(const std::vector<std::pair<std::string, std::string>> &kv)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const auto &[k, v] : kv)
            out[k] = nlohmann::json::parse(v);
        return out.dump();
    }

    // initializer_list overload for convenience
    inline std::string JsonObj(std::initializer_list<std::pair<const char *, std::string>> kv)
    {
        std::vector<std::pair<std::string, std::string>> vec;
        vec.reserve(kv.size());
        for (auto &[k, v] : kv)
            vec.emplace_back(k, v);
        return JsonObj(vec);
    }

    // Parse a tool-call args JSON object once; returns null json on failure
    inline nlohmann::json ParseArgs(const std::string &args)
    {
        try
        {
            return nlohmann::json::parse(args);
        }
        catch (...)
        {
            return {};
        }
    }

    // extract a JSON string value by key from a tool-call args object
    inline std::string ExtractArgStr(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return "";
    }

    // extract a JSON integer value by key from a tool-call args object
    inline int64_t ExtractArgInt(const std::string &args, const char *key, int64_t defaultVal = 0)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_number())
            return j[key].get<int64_t>();
        return defaultVal;
    }

    // extract a JSON string array by key from a tool-call args object
    inline std::vector<std::string> ExtractArgArray(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (!j.contains(key) || !j[key].is_array())
            return {};
        std::vector<std::string> result;
        for (const auto &item : j[key])
        {
            if (item.is_string())
                result.push_back(item.get<std::string>());
        }
        return result;
    }

    // extract a JSON number value by key from a tool-call args object
    inline float ExtractArgNum(const std::string &args, const char *key)
    {
        auto j = ParseArgs(args);
        if (j.contains(key) && j[key].is_number())
            return j[key].get<float>();
        return 0.0f;
    }

    // Base64 encode binary data
    inline std::string Base64Encode(const uint8_t *data, size_t len)
    {
        static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3)
        {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < len)
                n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < len)
                n |= static_cast<uint32_t>(data[i + 2]);
            out += table[(n >> 18) & 0x3F];
            out += table[(n >> 12) & 0x3F];
            out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
            out += (i + 2 < len) ? table[n & 0x3F] : '=';
        }
        return out;
    }

    // Base64 decode string data
    inline std::vector<uint8_t> Base64Decode(const std::string &in)
    {
        static constexpr int table[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
            -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1};

        std::vector<uint8_t> out;
        int val = 0, valb = -8;
        for (uint8_t c : in)
        {
            if (c > 127 || table[c] == -1)
                break;
            val = (val << 6) + table[c];
            valb += 6;
            if (valb >= 0)
            {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // Encode RGBA pixels as PNG via stb_image_write.
    std::vector<uint8_t> EncodeRGBA_PNG(const uint8_t *rgba, int w, int h);

    // Strip non-UTF-8 bytes to avoid JSON serialization errors (nlohmann type_error.316).
    // Invalid/truncated multi-byte sequences are dropped silently.
    inline std::string SanitizeUTF8(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            int len;
            if (c < 0x80)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            else
            {
                ++i;
                continue; // invalid lead byte — drop
            }

            if (i + static_cast<size_t>(len) > s.size())
            {
                ++i;
                continue; // truncated sequence — drop
            }

            bool valid = true;
            for (int j = 1; j < len; ++j)
            {
                if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
                out.append(s, i, len);
            i += valid ? static_cast<size_t>(len) : 1;
        }
        return out;
    }

    // Returns true if 'path' resolves to a location under 'allowedRoot'.
    // Rejects path traversal attempts (e.g. "../../etc/passwd").
    // Cross-platform: uses std::filesystem::path iteration, works with both / and \.
    inline bool IsPathSafe(const std::string &path, const std::string &allowedRoot)
    {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (ec)
            return false;
        auto root = std::filesystem::weakly_canonical(allowedRoot, ec);
        if (ec)
            return false;
        // Check that every component of root is a prefix of canonical
        auto rootIt = root.begin();
        auto pathIt = canonical.begin();
        for (; rootIt != root.end(); ++rootIt, ++pathIt)
        {
            if (pathIt == canonical.end() || *pathIt != *rootIt)
                return false;
        }
        return true;
    }
} // namespace pmcp
