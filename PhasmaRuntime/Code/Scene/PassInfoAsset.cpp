#include "Scene/PassInfoAsset.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/prettywriter.h"

namespace pe
{
    namespace
    {
        using JsonValue = rapidjson::Value;

        const JsonValue *FindMember(const JsonValue &object, const char *name)
        {
            if (!object.IsObject())
                return nullptr;

            auto it = object.FindMember(name);
            return it != object.MemberEnd() ? &it->value : nullptr;
        }

        std::string ReadString(const JsonValue &object, const char *name, const std::string &fallback = {})
        {
            const JsonValue *value = FindMember(object, name);
            return value && value->IsString() ? value->GetString() : fallback;
        }

        bool ReadBool(const JsonValue &object, const char *name, bool fallback)
        {
            const JsonValue *value = FindMember(object, name);
            return value && value->IsBool() ? value->GetBool() : fallback;
        }

        float ReadFloat(const JsonValue &object, const char *name, float fallback)
        {
            const JsonValue *value = FindMember(object, name);
            return value && value->IsNumber() ? value->GetFloat() : fallback;
        }

        uint32_t ReadUint(const JsonValue &object, const char *name, uint32_t fallback)
        {
            const JsonValue *value = FindMember(object, name);
            return value && value->IsUint() ? value->GetUint() : fallback;
        }

        void ReadStringArray(const JsonValue &object, const char *name, std::vector<std::string> &out)
        {
            const JsonValue *value = FindMember(object, name);
            if (!value || !value->IsArray())
                return;

            for (const JsonValue &entry : value->GetArray())
                if (entry.IsString())
                    out.emplace_back(entry.GetString());
        }

        void AddStringMember(JsonValue &object,
                             const char *name,
                             const std::string &value,
                             rapidjson::Document::AllocatorType &alloc)
        {
            object.AddMember(rapidjson::Value(name, alloc).Move(),
                             rapidjson::Value(value.c_str(), alloc).Move(),
                             alloc);
        }

        void AddStringArray(JsonValue &object,
                            const char *name,
                            const std::vector<std::string> &values,
                            rapidjson::Document::AllocatorType &alloc)
        {
            rapidjson::Value array(rapidjson::kArrayType);
            for (const std::string &value : values)
                array.PushBack(rapidjson::Value(value.c_str(), alloc).Move(), alloc);
            object.AddMember(rapidjson::Value(name, alloc).Move(), array.Move(), alloc);
        }
    } // namespace

    PassInfoAsset::PassInfoAsset() {}

    void PassInfoAsset::Load()
    {
        LoadFromFile(GetResourceId());
    }

    bool PassInfoAsset::LoadFromFile(const std::filesystem::path &path)
    {
        m_filePath = path;

        std::ifstream file(path);
        if (!file.is_open())
        {
            Log::Error(PeFormat("[PassInfoAsset] Failed to open: %s", path.string().c_str()));
            return false;
        }

        rapidjson::IStreamWrapper stream(file);
        rapidjson::Document document;
        document.ParseStream(stream);
        if (document.HasParseError() || !document.IsObject())
        {
            Log::Error(PeFormat("[PassInfoAsset] Parse error in %s", path.string().c_str()));
            return false;
        }

        m_name = ReadString(document, "name", path.stem().string());
        m_variants.clear();

        for (auto it = document.MemberBegin(); it != document.MemberEnd(); ++it)
        {
            std::string key = it->name.GetString();
            if (key == "name" || key == "version")
                continue;

            const JsonValue &val = it->value;
            if (!val.IsObject())
                continue;

            PassVariant variant;
            variant.vertexShader = ReadString(val, "vertexShader");
            variant.fragmentShader = ReadString(val, "fragmentShader");

            variant.cullMode = ReadString(val, "cullMode");
            variant.depthCompareOp = ReadString(val, "depthCompareOp");
            variant.depthWriteEnable = ReadBool(val, "depthWriteEnable", true);
            variant.depthTestEnable = ReadBool(val, "depthTestEnable", true);
            variant.blendEnable = ReadBool(val, "blendEnable", false);

            variant.topology = ReadString(val, "topology");
            variant.polygonMode = ReadString(val, "polygonMode");
            variant.lineWidth = ReadFloat(val, "lineWidth", 1.0f);

            variant.stencilTestEnable = ReadBool(val, "stencilTestEnable", false);
            variant.stencilFailOp = ReadString(val, "stencilFailOp");
            variant.stencilPassOp = ReadString(val, "stencilPassOp");
            variant.stencilDepthFailOp = ReadString(val, "stencilDepthFailOp");
            variant.stencilCompareOp = ReadString(val, "stencilCompareOp");
            variant.stencilCompareMask = ReadUint(val, "stencilCompareMask", 0xFFu);
            variant.stencilWriteMask = ReadUint(val, "stencilWriteMask", 0xFFu);
            variant.stencilReference = ReadUint(val, "stencilReference", 0u);

            ReadStringArray(val, "colorBlendAttachments", variant.colorBlendAttachments);
            ReadStringArray(val, "dynamicStates", variant.dynamicStates);
            ReadStringArray(val, "colorFormats", variant.colorFormats);
            variant.depthFormat = ReadString(val, "depthFormat");

            if (const JsonValue *mb = FindMember(val, "materialBinding"))
            {
                variant.materialBufferName = ReadString(*mb, "bufferName");
                variant.materialAnnotation = ReadString(*mb, "annotation");
            }

            m_variants[key] = std::move(variant);
        }

        return true;
    }

    void PassInfoAsset::SaveToFile(const std::filesystem::path &path) const
    {
        rapidjson::Document document;
        document.SetObject();
        auto &alloc = document.GetAllocator();

        AddStringMember(document, "name", m_name, alloc);
        document.AddMember("version", 1, alloc);

        for (auto &[key, variant] : m_variants)
        {
            rapidjson::Value variantJson(rapidjson::kObjectType);

            if (!variant.vertexShader.empty())
                AddStringMember(variantJson, "vertexShader", variant.vertexShader, alloc);
            if (!variant.fragmentShader.empty())
                AddStringMember(variantJson, "fragmentShader", variant.fragmentShader, alloc);

            if (!variant.cullMode.empty())
                AddStringMember(variantJson, "cullMode", variant.cullMode, alloc);
            if (!variant.depthCompareOp.empty())
                AddStringMember(variantJson, "depthCompareOp", variant.depthCompareOp, alloc);
            variantJson.AddMember("depthWriteEnable", variant.depthWriteEnable, alloc);
            variantJson.AddMember("depthTestEnable", variant.depthTestEnable, alloc);
            variantJson.AddMember("blendEnable", variant.blendEnable, alloc);

            if (!variant.topology.empty())
                AddStringMember(variantJson, "topology", variant.topology, alloc);
            if (!variant.polygonMode.empty())
                AddStringMember(variantJson, "polygonMode", variant.polygonMode, alloc);
            if (variant.lineWidth != 1.0f)
                variantJson.AddMember("lineWidth", variant.lineWidth, alloc);

            if (variant.stencilTestEnable)
            {
                variantJson.AddMember("stencilTestEnable", variant.stencilTestEnable, alloc);
                if (!variant.stencilFailOp.empty())
                    AddStringMember(variantJson, "stencilFailOp", variant.stencilFailOp, alloc);
                if (!variant.stencilPassOp.empty())
                    AddStringMember(variantJson, "stencilPassOp", variant.stencilPassOp, alloc);
                if (!variant.stencilDepthFailOp.empty())
                    AddStringMember(variantJson, "stencilDepthFailOp", variant.stencilDepthFailOp, alloc);
                if (!variant.stencilCompareOp.empty())
                    AddStringMember(variantJson, "stencilCompareOp", variant.stencilCompareOp, alloc);
                if (variant.stencilCompareMask != 0xFF)
                    variantJson.AddMember("stencilCompareMask", variant.stencilCompareMask, alloc);
                if (variant.stencilWriteMask != 0xFF)
                    variantJson.AddMember("stencilWriteMask", variant.stencilWriteMask, alloc);
                if (variant.stencilReference != 0)
                    variantJson.AddMember("stencilReference", variant.stencilReference, alloc);
            }

            if (!variant.colorBlendAttachments.empty())
                AddStringArray(variantJson, "colorBlendAttachments", variant.colorBlendAttachments, alloc);

            if (!variant.dynamicStates.empty())
                AddStringArray(variantJson, "dynamicStates", variant.dynamicStates, alloc);

            if (!variant.colorFormats.empty())
                AddStringArray(variantJson, "colorFormats", variant.colorFormats, alloc);
            if (!variant.depthFormat.empty())
                AddStringMember(variantJson, "depthFormat", variant.depthFormat, alloc);

            if (!variant.materialBufferName.empty())
            {
                rapidjson::Value materialBinding(rapidjson::kObjectType);
                AddStringMember(materialBinding, "bufferName", variant.materialBufferName, alloc);
                if (!variant.materialAnnotation.empty())
                    AddStringMember(materialBinding, "annotation", variant.materialAnnotation, alloc);
                variantJson.AddMember("materialBinding", materialBinding.Move(), alloc);
            }

            document.AddMember(rapidjson::Value(key.c_str(), alloc).Move(), variantJson.Move(), alloc);
        }

        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        rapidjson::OStreamWrapper stream(file);
        rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(stream);
        document.Accept(writer);
    }

    const PassVariant *PassInfoAsset::GetVariant(const std::string &name) const
    {
        auto it = m_variants.find(name);
        return (it != m_variants.end()) ? &it->second : nullptr;
    }

    bool PassInfoAsset::HasVariant(const std::string &name) const
    {
        return m_variants.contains(name);
    }
} // namespace pe
