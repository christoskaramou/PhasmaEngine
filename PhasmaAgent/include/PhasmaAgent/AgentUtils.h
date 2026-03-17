#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <filesystem>

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
                else if (next == 'u' && i + 4 < s.size())
                {
                    // Handle \uXXXX unicode escapes
                    std::string hex = s.substr(i + 1, 4);
                    try
                    {
                        unsigned long cp = std::stoul(hex, nullptr, 16);
                        if (cp < 0x80)
                            out += static_cast<char>(cp);
                        else if (cp < 0x800)
                        {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        i += 4;
                    }
                    catch (...)
                    {
                        out += '\\';
                        out += next;
                    }
                }
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
        // Walk forward skipping escaped characters to find the real closing quote
        size_t i = pos + 1;
        while (i < args.size())
        {
            if (args[i] == '\\')
            {
                i += 2; // skip escaped char
                continue;
            }
            if (args[i] == '"')
                break;
            ++i;
        }
        if (i >= args.size())
            return "";
        return args.substr(pos + 1, i - pos - 1);
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

    // Encode RGBA pixels as an uncompressed PNG.
    // Uses deflate stored blocks (no compression) — simple but valid PNG.
    inline std::vector<uint8_t> EncodeRGBA_PNG(const uint8_t *rgba, int w, int h)
    {
        auto crc32 = [](const uint8_t *data, size_t len) -> uint32_t
        {
            uint32_t c = 0xFFFFFFFF;
            for (size_t i = 0; i < len; i++)
            {
                c ^= data[i];
                for (int j = 0; j < 8; j++)
                    c = (c >> 1) ^ (0xEDB88320 & (-(c & 1)));
            }
            return c ^ 0xFFFFFFFF;
        };

        auto put32be = [](std::vector<uint8_t> &v, uint32_t val)
        {
            v.push_back((val >> 24) & 0xFF);
            v.push_back((val >> 16) & 0xFF);
            v.push_back((val >> 8) & 0xFF);
            v.push_back(val & 0xFF);
        };

        auto writeChunk = [&](std::vector<uint8_t> &out, const char *type, const uint8_t *data, size_t len)
        {
            put32be(out, static_cast<uint32_t>(len));
            size_t typeStart = out.size();
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(type[i]));
            out.insert(out.end(), data, data + len);
            uint32_t c = crc32(&out[typeStart], 4 + len);
            put32be(out, c);
        };

        std::vector<uint8_t> png;
        // PNG signature
        const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
        png.insert(png.end(), sig, sig + 8);

        // IHDR
        std::vector<uint8_t> ihdr;
        put32be(ihdr, w);
        put32be(ihdr, h);
        ihdr.push_back(8); // bit depth
        ihdr.push_back(6); // color type: RGBA
        ihdr.push_back(0); // compression
        ihdr.push_back(0); // filter
        ihdr.push_back(0); // interlace
        writeChunk(png, "IHDR", ihdr.data(), ihdr.size());

        // IDAT: build raw filtered data (filter byte 0 = None per row)
        size_t rowBytes = w * 4;
        size_t rawSize = h * (1 + rowBytes); // filter byte + pixel data per row
        std::vector<uint8_t> raw(rawSize);
        for (int y = 0; y < h; y++)
        {
            raw[y * (1 + rowBytes)] = 0; // filter: None
            std::memcpy(&raw[y * (1 + rowBytes) + 1], rgba + y * rowBytes, rowBytes);
        }

        // Wrap in zlib: CMF + FLG + stored deflate blocks + Adler32
        // Adler32
        uint32_t a = 1, b = 0;
        for (size_t i = 0; i < raw.size(); i++)
        {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
        }
        uint32_t adler = (b << 16) | a;

        std::vector<uint8_t> zlib;
        zlib.push_back(0x78); // CMF
        zlib.push_back(0x01); // FLG (no dict, check bits)

        // Split into 65535-byte stored blocks
        size_t offset = 0;
        while (offset < raw.size())
        {
            size_t blockLen = std::min<size_t>(raw.size() - offset, 65535);
            bool last = (offset + blockLen >= raw.size());
            zlib.push_back(last ? 0x01 : 0x00); // BFINAL + BTYPE=00 (stored)
            uint16_t len16 = static_cast<uint16_t>(blockLen);
            uint16_t nlen16 = ~len16;
            zlib.push_back(len16 & 0xFF);
            zlib.push_back((len16 >> 8) & 0xFF);
            zlib.push_back(nlen16 & 0xFF);
            zlib.push_back((nlen16 >> 8) & 0xFF);
            zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
            offset += blockLen;
        }
        put32be(zlib, adler);

        writeChunk(png, "IDAT", zlib.data(), zlib.size());

        // IEND
        writeChunk(png, "IEND", nullptr, 0);

        return png;
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
} // namespace pagent
