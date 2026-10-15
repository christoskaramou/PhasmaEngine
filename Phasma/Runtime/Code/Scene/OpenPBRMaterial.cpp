#include "Scene/OpenPBRMaterial.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/SceneNode.h"
#include "gtc/color_space.hpp"
#include "rapidjson/document.h"

namespace pe
{
    namespace
    {
        struct OpenPBRSchemaEntry
        {
            const char *input;
            const char *type;
            uint32_t offset;
        };

        // The default module's layout as PhasmaCook generates it from MaterialX 1.39.5's open_pbr_surface (prototype
        // output 2026-10-13; Task 8's test pins the same 37 inputs / 220 bytes). The generated loader reads exactly these
        // offsets, so a .json that differs in any entry would make the GPU read past or beside the packed bytes.
        // A MaterialX upgrade regenerates the module: update this table with it.
        constexpr uint32_t kOpenPBRDefaultBytes = 220;
        constexpr OpenPBRSchemaEntry kOpenPBRDefaultSchema[] = {
            {"base_weight", "float", 0},
            {"base_color", "float3", 4},
            {"base_diffuse_roughness", "float", 16},
            {"base_metalness", "float", 20},
            {"specular_weight", "float", 24},
            {"specular_color", "float3", 28},
            {"specular_roughness", "float", 40},
            {"specular_ior", "float", 44},
            {"specular_roughness_anisotropy", "float", 48},
            {"transmission_weight", "float", 52},
            {"transmission_color", "float3", 56},
            {"transmission_depth", "float", 68},
            {"transmission_scatter", "float3", 72},
            {"transmission_scatter_anisotropy", "float", 84},
            {"transmission_dispersion_scale", "float", 88},
            {"transmission_dispersion_abbe_number", "float", 92},
            {"subsurface_weight", "float", 96},
            {"subsurface_color", "float3", 100},
            {"subsurface_radius", "float", 112},
            {"subsurface_radius_scale", "float3", 116},
            {"subsurface_scatter_anisotropy", "float", 128},
            {"fuzz_weight", "float", 132},
            {"fuzz_color", "float3", 136},
            {"fuzz_roughness", "float", 148},
            {"coat_weight", "float", 152},
            {"coat_color", "float3", 156},
            {"coat_roughness", "float", 168},
            {"coat_roughness_anisotropy", "float", 172},
            {"coat_ior", "float", 176},
            {"coat_darkening", "float", 180},
            {"thin_film_weight", "float", 184},
            {"thin_film_thickness", "float", 188},
            {"thin_film_ior", "float", 192},
            {"emission_luminance", "float", 196},
            {"emission_color", "float3", 200},
            {"geometry_opacity", "float", 212},
            {"geometry_thin_walled", "bool", 216},
        };
    } // namespace

    const OpenPBRInput *OpenPBRLayout::Find(const std::string &input) const
    {
        for (const OpenPBRInput &in : inputs)
        {
            if (in.input == input)
                return &in;
        }

        return nullptr;
    }

    const OpenPBRLayout &GetOpenPBRDefaultLayout()
    {
        static const OpenPBRLayout layout = []() -> OpenPBRLayout
        {
            const std::string dir = Path::RuntimeAssetsPath() + "Shaders/MaterialX/Generated/";
            const std::string path = dir + "PeOpenPBRDefault.json";

            if (!AssetFileExists(dir + "PeOpenPBRDefault.slang"))
            {
                PE_WARN("[OpenPBR] %sPeOpenPBRDefault.slang is missing (PhasmaCook --mtlx generates it at build time); MaterialX materials keep their legacy shading",
                        dir.c_str());
                return {};
            }

            FileSystem file(path, std::ios::in | std::ios::ate);
            if (!AssetFileExists(path) || !file.IsOpen())
            {
                PE_WARN("[OpenPBR] %s is missing (PhasmaCook --mtlx generates it at build time); MaterialX materials keep their legacy shading",
                        path.c_str());
                return {};
            }

            const std::string text = file.ReadAll();
            rapidjson::Document doc;
            doc.Parse(text.c_str());

            if (doc.HasParseError() ||
                !doc.IsObject() ||
                !doc.HasMember("inputs") ||
                !doc["inputs"].IsArray() ||
                !doc.HasMember("byteSize") ||
                !doc["byteSize"].IsUint() ||
                doc["byteSize"].GetUint() != kOpenPBRDefaultBytes ||
                !doc.HasMember("thin") ||
                !doc["thin"].IsBool() ||
                !doc["thin"].GetBool() ||
                doc["inputs"].GetArray().Size() != std::size(kOpenPBRDefaultSchema))
            {
                PE_WARN("[OpenPBR] %s is not a PhasmaCook material layout", path.c_str());
                return {};
            }

            OpenPBRLayout result;
            result.byteSize = doc["byteSize"].GetUint();

            for (const auto &item : doc["inputs"].GetArray())
            {
                if (!item.IsObject() ||
                    !item.HasMember("input") ||
                    !item["input"].IsString() ||
                    !item.HasMember("type") ||
                    !item["type"].IsString() ||
                    !item.HasMember("offset") ||
                    !item["offset"].IsUint() ||
                    !item.HasMember("default") ||
                    !item["default"].IsArray())
                {
                    PE_WARN("[OpenPBR] %s has a malformed input entry", path.c_str());
                    return {};
                }

                OpenPBRInput in;
                in.input = item["input"].GetString();
                in.type = item["type"].GetString();
                in.offset = item["offset"].GetUint();

                for (const auto &v : item["default"].GetArray())
                {
                    if (!v.IsNumber())
                    {
                        PE_WARN("[OpenPBR] %s: input %s has a non-numeric default", path.c_str(), in.input.c_str());
                        return {};
                    }

                    in.defaults.push_back(v.GetFloat());
                }

                // Entry i must equal the pinned schema's entry i (name, type, offset): that also rules out duplicates,
                // overlaps, gaps and offsets outside the 220 bytes the generated loader reads.
                const OpenPBRSchemaEntry &expected = kOpenPBRDefaultSchema[result.inputs.size()];
                const uint32_t width = in.type == "float3" ? 3u : 1u;

                if (in.input != expected.input ||
                    in.type != expected.type ||
                    in.offset != expected.offset ||
                    in.defaults.size() != width)
                {
                    PE_WARN("[OpenPBR] %s: input %s (%s @ %u) does not match the default module's layout (expected %s %s @ %u)",
                            path.c_str(), in.input.c_str(), in.type.c_str(), in.offset, expected.input, expected.type, expected.offset);
                    return {};
                }

                result.inputs.push_back(in);
            }

            result.valid = true; // every pinned entry matched, in order
            return result;
        }();

        return layout;
    }

    bool OpenPBREligible(Material &material)
    {
        const PassInfoAsset *pass = material.passInfoAsset.get();
        const bool standard = !pass || pass->GetFilePath().filename() == "standard_pbr.pass";

        return standard && GetOpenPBRDefaultLayout().valid;
    }

    bool MeshUsesOpenPBR(const Mesh &mesh)
    {
        return mesh.material &&
               mesh.materialInstance &&
               mesh.materialInstance->GetOpenPBR() &&
               OpenPBREligible(*mesh.material);
    }

    void ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input, float out[3])
    {
        out[0] = input.defaults.size() > 0 ? input.defaults[0] : 0.0f;
        out[1] = input.defaults.size() > 1 ? input.defaults[1] : 0.0f;
        out[2] = input.defaults.size() > 2 ? input.defaults[2] : 0.0f;

        MaterialParamValue value;
        if (instance.HasParamOverride(input.input))
        {
            value = instance.GetParam(input.input);
        }
        else if (auto it = instance.GetParent()->params.find(input.input);
                 it != instance.GetParent()->params.end())
        {
            value = it->second;
        }
        else
        {
            return;
        }

        if (const float *f = std::get_if<float>(&value))
        {
            out[0] = *f;
        }
        else if (const vec3 *v = std::get_if<vec3>(&value))
        {
            out[0] = v->x;
            out[1] = v->y;
            out[2] = v->z;
        }
        else if (const uint32_t *u = std::get_if<uint32_t>(&value))
        {
            out[0] = *u != 0u ? 1.0f : 0.0f;
        }
        else if (const int32_t *i = std::get_if<int32_t>(&value))
        {
            out[0] = *i != 0 ? 1.0f : 0.0f;
        }
    }

    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance)
    {
        const OpenPBRLayout &layout = GetOpenPBRDefaultLayout();
        std::vector<uint8_t> bytes(kOpenPBRInputsOffset + (layout.valid ? layout.byteSize : 0u), 0);

        const MaterialGpuData legacy = instance.BuildGpuData();
        std::memcpy(bytes.data(), &legacy, sizeof(legacy));

        for (const OpenPBRInput &in : layout.inputs)
        {
            float values[3];
            ResolveOpenPBRInput(instance, in, values);

            uint8_t *dst = bytes.data() + kOpenPBRInputsOffset + in.offset;
            if (in.type == "bool")
            {
                const uint32_t b = values[0] != 0.0f ? 1u : 0u;
                std::memcpy(dst, &b, sizeof(b));
            }
            else
            {
                std::memcpy(dst, values, (in.type == "float3" ? 3u : 1u) * sizeof(float));
            }
        }

        return bytes;
    }

    void MirrorOpenPBRToLegacy(MaterialInstance &instance)
    {
        const OpenPBRLayout &layout = GetOpenPBRDefaultLayout();
        auto value = [&](const char *name) -> vec3
        {
            float v[3] = {0.0f, 0.0f, 0.0f};
            if (const OpenPBRInput *in = layout.Find(name))
                ResolveOpenPBRInput(instance, *in, v);

            return vec3(v[0], v[1], v[2]);
        };

        if (!layout.valid)
            return;

        // Legacy colours are sRGB-authored (SceneColor decodes them under linear_color); OpenPBR's are linear.
        const vec3 base = glm::clamp(value("base_color") * value("base_weight").x, vec3(0.0f), vec3(1.0f));
        instance.SetBaseColorFactor(vec4(glm::convertLinearToSRGB(base), instance.GetBaseColorFactor().a));

        instance.SetMetallic(value("base_metalness").x);
        instance.SetRoughness(value("specular_roughness").x);
        instance.SetIor(value("specular_ior").x);

        // Legacy emissive is an sRGB-authored colour whose peak above 1 carries brightness (SrgbToLinearHdr, Common.hlsl:294;
        // GBufferPS.hlsl:93, RayTrace.hlsl:935): store the inverse of OpenPBR's linear emission_color * emission_luminance
        // (MaterialX's own emission weight, emission_weight_out in the generated module).
        const vec3 emission = glm::max(value("emission_color") * value("emission_luminance").x, vec3(0.0f));
        const float peak = std::max({emission.r, emission.g, emission.b, 1.0f});
        instance.SetEmissiveFactor(glm::convertLinearToSRGB(emission / peak) * peak);
    }
} // namespace pe
