#include "Scene/OpenPBRShapes.h"

namespace pe
{
    namespace
    {
        constexpr std::string_view kBegin = "// PE_SHAPE_BEGIN";
        constexpr std::string_view kEnd = "// PE_SHAPE_END";
        constexpr std::string_view kSpace = " \t\r\n\v\f";

        // Every API inlines the shapes. [noinline] shape calls on SPIR-V cost Vulkan raster 4x MaterialShade and 10x
        // MaterialThin GPU time even with one shape, and in RT hits returned wrong values and lost the device
        // (RTX 4080 SUPER, driver 617.14, 2026-11-01).
        constexpr const char *kShapeFnMacro = "#define PE_SHAPE_FN";

        struct Item
        {
            std::string text;
            bool shape = false;
        };

        struct Dispatcher
        {
            const char *ret;
            const char *name;
            const char *params;
            const char *args;
            const char *zero; // nullptr: void; "{}": a zero-initialized local
            bool load;        // the case loads the shape's material first (b, o)
            bool fill;        // ... and fills the vertex data (sp)
        };

        // A shape's statics are loaded in the same case that reads them. Loaded in one switch and read in another, every
        // shape's statics stayed live across MaterialShade's quadrant loop: 411 values for 8 shapes, spilled per pixel
        // (24.5 ms against 3.8; RTX 4080 SUPER, 2026-11-03). pe_fill_vertex_data writes only the shared vd.
        constexpr const char *kLoadParams = "ByteAddressBuffer b, uint o, PeSurfacePoint sp, float2 nextUv, float blend";
        constexpr Dispatcher kDispatchers[] = {
            {"void", "pe_fill_vertex_data", "PeSurfacePoint sp", "sp", nullptr, false, false},
            {"PeSurfaceInputs", "pe_material_inputs", kLoadParams, "nextUv, blend", "{}", true, true},
            {"float", "pe_material_mask", kLoadParams, "sp, nextUv, blend", "0.0", true, false},
            {"PeThin", "pe_material_thin", kLoadParams, "sp, nextUv, blend", "{}", true, false},
        };

        // The per-shape entry functions that get PE_SHAPE_FN.
        constexpr std::string_view kShapeFnReturns[] = {"void ", "PeSurfaceInputs ", "float ", "PeThin ", "float4 "};
        constexpr std::string_view kShapeFnNames[] = {"pe_load_material(", "pe_fill_vertex_data(", "pe_material_inputs(",
                                                      "pe_material_mask(", "pe_material_thin(", "pe_material_shade(",
                                                      "pe_evaluate_inputs("};

        std::string_view Strip(std::string_view s)
        {
            const size_t first = s.find_first_not_of(kSpace);
            if (first == std::string_view::npos)
                return {};

            return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
        }

        std::string_view RStrip(std::string_view s)
        {
            const size_t last = s.find_last_not_of(kSpace);
            return last == std::string_view::npos ? std::string_view{} : s.substr(0, last + 1);
        }

        // Lines as Python's str.split("\n"), without the empty piece after a final newline.
        std::vector<std::string_view> Lines(std::string_view text)
        {
            std::vector<std::string_view> lines;
            size_t start = 0;
            while (start < text.size())
            {
                const size_t end = text.find('\n', start);
                if (end == std::string_view::npos)
                {
                    lines.push_back(text.substr(start));
                    break;
                }

                lines.push_back(text.substr(start, end - start));
                start = end + 1;
            }

            return lines;
        }

        std::string Join(const std::vector<std::string> &parts, std::string_view separator)
        {
            std::string out;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i)
                    out += separator;

                out += parts[i];
            }

            return out;
        }

        bool IsWord(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        // Top-level items: a column-0 line starts one unless it opens or closes a brace; braces, a top-level #if ...
        // #endif and backslash continuations keep everything up to their close in the same item.
        std::vector<Item> SplitItems(const std::string &text)
        {
            std::vector<Item> out;
            std::vector<std::string_view> current;
            bool shape = false;
            int braces = 0;
            int conditionals = 0;
            bool continued = false;

            auto flush = [&]
            {
                while (!current.empty() && Strip(current.back()).empty())
                    current.pop_back();

                if (!current.empty())
                {
                    std::string joined;
                    for (size_t i = 0; i < current.size(); ++i)
                    {
                        if (i)
                            joined += '\n';

                        joined += current[i];
                    }

                    out.push_back({std::move(joined), shape});
                }

                current.clear();
            };

            for (std::string_view line : Lines(text))
            {
                const bool top = braces == 0 && conditionals == 0 && !continued;
                if (line == kBegin || line == kEnd)
                {
                    if (!top)
                        throw std::runtime_error("PE_SHAPE marker inside a block");

                    flush();
                    shape = line == kBegin;
                    continue;
                }

                const bool column0 = !line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '{' && line[0] != '}';
                if (top && column0)
                    flush();

                if (!current.empty() || !Strip(line).empty())
                    current.push_back(line);

                const std::string_view code = line.substr(0, line.find("//"));
                const std::string_view stripped = Strip(code);
                continued = RStrip(code).ends_with('\\') && (continued || stripped.starts_with('#'));
                if (stripped.starts_with("#if"))
                {
                    conditionals++;
                }
                else if (stripped.starts_with("#endif"))
                {
                    conditionals--;
                }
                else if (!stripped.starts_with('#'))
                {
                    braces += static_cast<int>(std::count(code.begin(), code.end(), '{'));
                    braces -= static_cast<int>(std::count(code.begin(), code.end(), '}'));
                }

                if (braces < 0 || conditionals < 0)
                    throw std::runtime_error("unbalanced braces or #if in a MaterialX module");
            }

            flush();
            if (braces != 0 || conditionals != 0 || shape)
                throw std::runtime_error("unbalanced MaterialX module");

            return out;
        }

        // `static [const] <type> <name>` followed by ';' or '=' (mtlx_combine.py STATIC).
        std::optional<std::string_view> StaticName(std::string_view line)
        {
            auto spaces = [&](size_t &i)
            {
                const size_t start = i;
                while (i < line.size() && kSpace.find(line[i]) != std::string_view::npos)
                    i++;

                return i > start;
            };
            auto word = [&](size_t &i)
            {
                const size_t start = i;
                while (i < line.size() && IsWord(line[i]))
                    i++;

                return line.substr(start, i - start);
            };

            if (!line.starts_with("static"))
                return std::nullopt;

            size_t i = 6;
            if (!spaces(i))
                return std::nullopt;

            // The optional "const " group, then <type> <name>: try with it, then without (the regex backtracks).
            for (const bool withConst : {true, false})
            {
                size_t j = i;
                if (withConst)
                {
                    if (word(j) != "const" || !spaces(j))
                        continue;
                }

                if (word(j).empty() || !spaces(j))
                    continue;

                const std::string_view name = word(j);
                if (name.empty())
                    continue;

                spaces(j);
                if (j < line.size() && (line[j] == ';' || line[j] == '='))
                    return name;
            }

            return std::nullopt;
        }

        // Each identifier in `names` gets the pe_s<slot>_ prefix (whole words only).
        std::string Rename(const std::string &text, const std::unordered_set<std::string_view> &names, uint32_t slot)
        {
            const std::string prefix = "pe_s" + std::to_string(slot) + "_";
            std::string out;
            out.reserve(text.size());
            size_t i = 0;
            while (i < text.size())
            {
                if (!IsWord(text[i]))
                {
                    out += text[i++];
                    continue;
                }

                const size_t start = i;
                while (i < text.size() && IsWord(text[i]))
                    i++;

                const std::string_view word(text.data() + start, i - start);
                if (names.contains(word))
                    out += prefix;

                out += word;
            }

            return out;
        }

        bool IsShapeFnLine(std::string_view line)
        {
            for (std::string_view ret : kShapeFnReturns)
            {
                if (!line.starts_with(ret))
                    continue;

                const std::string_view rest = line.substr(ret.size());
                for (std::string_view name : kShapeFnNames)
                {
                    if (rest.starts_with(name))
                        return true;
                }
            }

            return false;
        }

        std::string MarkShapeFns(const std::string &text)
        {
            std::vector<std::string> lines;
            for (std::string_view line : Lines(text))
            {
                if (IsShapeFnLine(line))
                    lines.emplace_back("PE_SHAPE_FN");

                lines.emplace_back(line);
            }

            return Join(lines, "\n");
        }

        std::string DispatcherText(const Dispatcher &d, const std::vector<uint32_t> &slots)
        {
            const std::string ret = d.ret;
            const bool isVoid = d.zero == nullptr;
            std::string text = ret + " " + d.name + "(uint shape, " + d.params + ")\n{\n    switch (shape)\n    {\n";
            for (uint32_t slot : slots)
            {
                const std::string space = "pe_shape_" + std::to_string(slot) + "::";
                const std::string call = space + d.name + "(" + d.args + ")";
                text += "    case " + std::to_string(slot) + ":\n";
                if (d.load)
                    text += "        " + space + "pe_load_material(b, o);\n";

                if (d.fill)
                    text += "        " + space + "pe_fill_vertex_data(sp);\n";

                text += isVoid ? "        " + call + ";\n        return;\n" : "        return " + call + ";\n";
            }

            text += "    default:\n";
            if (isVoid)
                text += "        return;\n";
            else if (std::string_view(d.zero) == "{}")
                text += "        " + ret + " zero = {};\n        return zero;\n";
            else
                text += "        return " + std::string(d.zero) + ";\n";

            text += "    }\n}";
            return text;
        }
    } // namespace

    std::string CombineOpenPBRShapes(const std::string &defaultCode, const std::vector<OpenPBRShapeCode> &members,
                                     uint32_t slots)
    {
        std::vector<std::string> shared;
        std::unordered_set<std::string> known;
        for (Item &item : SplitItems(defaultCode))
        {
            if (!item.shape)
            {
                known.insert(item.text);
                shared.push_back(std::move(item.text));
            }
        }

        // Code outside the regions that two or more members have and the default lacks compiles once, first seen first.
        std::vector<std::vector<Item>> perShape;
        std::unordered_map<std::string, uint32_t> extra;
        for (const OpenPBRShapeCode &member : members)
        {
            std::vector<Item> &items = perShape.emplace_back(SplitItems(member.code));
            std::unordered_set<std::string> own;
            for (const Item &item : items)
            {
                if (!item.shape && !known.contains(item.text))
                    own.insert(item.text);
            }

            for (const std::string &text : own)
                extra[text]++;
        }

        std::vector<std::string> hoisted;
        for (const std::vector<Item> &items : perShape)
        {
            for (const Item &item : items)
            {
                const auto count = extra.find(item.text);
                const bool byShapes = !item.shape && count != extra.end() && count->second > 1;
                if (byShapes && !known.contains(item.text))
                {
                    known.insert(item.text);
                    hoisted.push_back(item.text);
                }
            }
        }

        std::vector<std::string> order(slots, "-");
        for (const OpenPBRShapeCode &member : members)
            order.at(member.slot) = member.hash;

        std::vector<std::string> parts = {"// PE_SHAPES " + Join(order, " "), kShapeFnMacro};
        parts.insert(parts.end(), shared.begin(), shared.end());
        parts.insert(parts.end(), hoisted.begin(), hoisted.end());

        std::vector<uint32_t> present;
        for (size_t m = 0; m < members.size(); ++m)
        {
            const uint32_t slot = members[m].slot;
            present.push_back(slot);

            std::vector<std::string> own;
            for (const Item &item : perShape[m])
            {
                if (item.shape || !known.contains(item.text))
                    own.push_back(item.text);
            }

            // With -no-mangle, slangc's HLSL drops the namespace from globals: each member's statics need their own names.
            std::unordered_set<std::string_view> names; // views into `own`, which stays unchanged below
            for (const std::string &text : own)
            {
                for (std::string_view line : Lines(text))
                {
                    if (const auto name = StaticName(line))
                        names.insert(*name);
                }
            }

            std::vector<std::string> body;
            for (const std::string &text : own)
                body.push_back(MarkShapeFns(names.empty() ? text : Rename(text, names, slot)));

            parts.push_back("namespace pe_shape_" + std::to_string(slot) + "\n{\n" + Join(body, "\n\n") + "\n}");
        }

        for (const Dispatcher &d : kDispatchers)
            parts.push_back(DispatcherText(d, present));

        return Join(parts, "\n\n") + "\n";
    }
} // namespace pe
