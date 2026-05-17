#pragma once

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace pe::project_detail
{
    inline void SetError(std::string *error, std::string message)
    {
        if (error)
            *error = std::move(message);
    }

    inline std::filesystem::path Normalize(const std::filesystem::path &path)
    {
        return path.lexically_normal();
    }

    inline std::filesystem::path NormalizeAbsolute(const std::filesystem::path &path)
    {
        if (path.empty())
            return {};

        std::error_code ec;
        const std::filesystem::path absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
        return Normalize(ec ? path : absolutePath);
    }

    inline std::filesystem::path NormalizeAgainst(const std::filesystem::path &root,
                                                  const std::filesystem::path &path)
    {
        if (path.empty() || path.is_absolute())
            return Normalize(path);

        return Normalize(root / path);
    }

    inline void SkipUtf8Bom(std::istream &stream)
    {
        char bom[3]{};
        stream.read(bom, sizeof(bom));
        if (stream.gcount() != sizeof(bom) ||
            static_cast<unsigned char>(bom[0]) != 0xef ||
            static_cast<unsigned char>(bom[1]) != 0xbb ||
            static_cast<unsigned char>(bom[2]) != 0xbf)
        {
            stream.clear();
            stream.seekg(0, std::ios::beg);
        }
    }

    inline bool TryLoadJsonObject(const std::filesystem::path &path,
                                  rapidjson::Document &document,
                                  std::string &warning)
    {
        document.SetObject();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return true;

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            warning = "Could not open " + path.generic_string();
            return false;
        }

        SkipUtf8Bom(file);
        rapidjson::IStreamWrapper stream(file);
        document.ParseStream(stream);
        if (document.HasParseError())
        {
            warning = "Could not parse " + path.generic_string() + " at offset " +
                      std::to_string(document.GetErrorOffset()) + ": " +
                      rapidjson::GetParseError_En(document.GetParseError());
            document.SetObject();
            return false;
        }

        if (!document.IsObject())
        {
            warning = path.generic_string() + " must contain a JSON object";
            document.SetObject();
            return false;
        }

        return true;
    }

    inline std::string ReadJsonStringField(const rapidjson::Document &document, const char *key)
    {
        if (!document.HasMember(key) || !document[key].IsString())
            return {};

        return document[key].GetString();
    }

    inline void SetJsonStringMember(rapidjson::Document &document, const char *key, const std::string &value)
    {
        rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
        rapidjson::Value jsonKey(key, allocator);
        rapidjson::Value jsonValue;
        jsonValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);

        if (document.HasMember(key))
            document[key] = jsonValue;
        else
            document.AddMember(jsonKey.Move(), jsonValue.Move(), allocator);
    }

    inline bool WriteJsonObject(const std::filesystem::path &path,
                                const rapidjson::Document &document,
                                std::string *error)
    {
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                SetError(error, "Could not create " + parent.generic_string() + ": " + ec.message());
                return false;
            }
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            SetError(error, "Could not open " + path.generic_string() + " for writing");
            return false;
        }

        rapidjson::OStreamWrapper stream(file);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(stream);
        writer.SetIndent(' ', 2);
        if (!document.Accept(writer))
        {
            SetError(error, "Could not serialize " + path.generic_string());
            return false;
        }

        file << '\n';
        return true;
    }
} // namespace pe::project_detail
