#pragma once

namespace pe
{
    enum class MaterialWidgetHint
    {
        Auto,
        Color3,
        Color4,
        Range01,
        Range,
        Normal,
        SRGB,
    };

    struct MaterialFieldHint
    {
        std::string fieldName;
        MaterialWidgetHint widget = MaterialWidgetHint::Auto;
        float rangeMin = 0.f;
        float rangeMax = 1.f;
        std::string structMember; // e.g. "pbrParams"
        int component = -1;       // swizzle index: 0=x,1=y,2=z,3=w; -1 = whole member
    };

    enum class MaterialAnnotationType
    {
        None,
        MaterialTable,
        Texture,
    };

    struct MaterialAnnotation
    {
        MaterialAnnotationType type = MaterialAnnotationType::None;
        std::vector<MaterialFieldHint> fieldHints;
    };

    inline MaterialAnnotation ParseMaterialAnnotation(const std::string &semantic)
    {
        MaterialAnnotation result;
        if (semantic.empty())
            return result;

        std::vector<std::string> tokens;
        {
            size_t start = 0;
            while (start < semantic.size())
            {
                size_t end = semantic.find(';', start);
                if (end == std::string::npos)
                    end = semantic.size();
                tokens.push_back(semantic.substr(start, end - start));
                start = end + 1;
            }
        }

        if (tokens.empty())
            return result;

        if (tokens[0] == "pe_material")
            result.type = MaterialAnnotationType::MaterialTable;
        else if (tokens[0] == "pe_texture")
            result.type = MaterialAnnotationType::Texture;
        else
            return result;

        auto parseMapping = [](std::string &name)
            -> std::pair<std::string, int>
        {
            // Parse "fieldName=structMember.x" -> extracts structMember + component, strips from name
            size_t eq = name.find('=');
            if (eq == std::string::npos)
                return {"", -1};

            std::string mapping = name.substr(eq + 1);
            name = name.substr(0, eq);

            std::string member = mapping;
            int comp = -1;
            size_t dot = mapping.rfind('.');
            if (dot != std::string::npos && dot + 1 < mapping.size())
            {
                char c = mapping[dot + 1];
                if (c == 'x' || c == 'r')
                    comp = 0;
                else if (c == 'y' || c == 'g')
                    comp = 1;
                else if (c == 'z' || c == 'b')
                    comp = 2;
                else if (c == 'w' || c == 'a')
                    comp = 3;
                if (comp >= 0)
                    member = mapping.substr(0, dot);

                // Check for multi-component swizzle like ".xyz"
                std::string swiz = mapping.substr(dot + 1);
                if (swiz == "xyz" || swiz == "rgb")
                    comp = -2; // special: first 3 components
            }
            return {member, comp};
        };

        for (size_t i = 1; i < tokens.size(); i++)
        {
            const std::string &tok = tokens[i];
            size_t colon1 = tok.find(':');
            if (colon1 == std::string::npos)
                continue;

            std::string hint = tok.substr(0, colon1);
            std::string rest = tok.substr(colon1 + 1);

            MaterialFieldHint fh;

            if (hint == "color4")
                fh.widget = MaterialWidgetHint::Color4;
            else if (hint == "color3")
                fh.widget = MaterialWidgetHint::Color3;
            else if (hint == "range01")
                fh.widget = MaterialWidgetHint::Range01;
            else if (hint == "sRGB" || hint == "srgb")
                fh.widget = MaterialWidgetHint::SRGB;
            else if (hint == "normal")
                fh.widget = MaterialWidgetHint::Normal;
            else if (hint == "range")
            {
                fh.widget = MaterialWidgetHint::Range;
                size_t colon2 = rest.find(':');
                if (colon2 != std::string::npos)
                {
                    std::string nameAndMapping = rest.substr(0, colon2);
                    auto [member, comp] = parseMapping(nameAndMapping);
                    fh.fieldName = nameAndMapping;
                    fh.structMember = member;
                    fh.component = comp;
                    std::string minmax = rest.substr(colon2 + 1);
                    size_t colon3 = minmax.find(':');
                    if (colon3 != std::string::npos)
                    {
                        fh.rangeMin = std::stof(minmax.substr(0, colon3));
                        fh.rangeMax = std::stof(minmax.substr(colon3 + 1));
                    }
                    result.fieldHints.push_back(fh);
                    continue;
                }
            }

            // Parse "fieldName=structMember.component" mapping
            auto [member, comp] = parseMapping(rest);
            fh.structMember = member;
            fh.component = comp;

            if (fh.fieldName.empty())
                fh.fieldName = rest;

            result.fieldHints.push_back(fh);
        }

        return result;
    }
} // namespace pe
