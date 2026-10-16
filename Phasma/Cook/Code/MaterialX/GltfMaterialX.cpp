#include "GltfMaterialX.h"
#include "API/Image.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "gtc/color_space.hpp"
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace pe
{
    namespace
    {
        namespace mx = MaterialX;
        using Json = rapidjson::Value;

        const Json &Member(const Json &value, const char *key)
        {
            static const Json missing;
            if (value.IsNull())
                return missing;
            if (!value.IsObject())
                throw std::runtime_error(std::string("expected object for ") + key);

            const auto it = value.FindMember(key);
            return it == value.MemberEnd() ? missing : it->value;
        }

        float Number(const Json &value, float fallback)
        {
            if (value.IsNull())
                return fallback;
            if (!value.IsNumber() || !std::isfinite(value.GetDouble()))
                throw std::runtime_error("expected finite number");

            const float result = value.GetFloat();
            if (!std::isfinite(result))
                throw std::runtime_error("glTF number exceeds float range");

            return result;
        }

        std::string String(const Json &value, const std::string &fallback = {})
        {
            if (value.IsNull())
                return fallback;
            if (!value.IsString())
                throw std::runtime_error("expected string");

            return {value.GetString(), value.GetStringLength()};
        }

        size_t Index(const Json &value)
        {
            if (!value.IsUint())
                throw std::runtime_error("expected unsigned index");

            return value.GetUint();
        }

        const Json &At(const Json &array, size_t index)
        {
            if (!array.IsArray() || index >= array.Size())
                throw std::runtime_error("glTF index out of bounds");

            return array[static_cast<rapidjson::SizeType>(index)];
        }

        mx::Color3 Color(const Json &value, const mx::Color3 &fallback)
        {
            if (value.IsNull())
                return fallback;
            if (!value.IsArray() || value.Size() < 3)
                throw std::runtime_error("expected RGB array");

            return {Number(value[0], 0), Number(value[1], 0), Number(value[2], 0)};
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto u8 = path.generic_u8string();
            return {reinterpret_cast<const char *>(u8.data()), u8.size()};
        }

        std::string ReadBytes(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("cannot read " + Utf8(path));

            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        void WriteBytes(const std::filesystem::path &path, const std::string &bytes)
        {
            std::filesystem::create_directories(path.parent_path());
            auto temp = path;
            temp += ".tmp";
            FileSystem stream(Utf8(temp), std::ios::out | std::ios::binary | std::ios::trunc);
            if (!stream.IsOpen() || !stream.Write(bytes.data(), bytes.size()) || !stream.Flush())
                throw std::runtime_error("cannot write " + Utf8(temp));

            stream.Close();
            if (!FileSystem::ReplaceFile(temp, path))
                throw std::runtime_error("cannot replace " + Utf8(path));
        }

        std::string DecodeUri(const std::string &uri)
        {
            std::string decoded;
            for (size_t i = 0; i < uri.size(); ++i)
            {
                if (uri[i] != '%')
                {
                    decoded += uri[i];
                    continue;
                }

                if (i + 2 >= uri.size() || !std::isxdigit(static_cast<unsigned char>(uri[i + 1])) ||
                    !std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
                    throw std::runtime_error("invalid escaped glTF URI");

                decoded += static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            return decoded;
        }

        std::string ReadUri(const std::filesystem::path &directory, const std::string &uri)
        {
            if (uri.rfind("data:", 0) != 0)
                return ReadBytes(directory / std::filesystem::u8path(DecodeUri(uri)));

            const size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos)
                throw std::runtime_error("unsupported glTF data URI");

            const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string bytes;
            uint32_t accumulator = 0;
            int bits = 0;
            for (size_t i = comma + 1; i < uri.size() && uri[i] != '='; ++i)
            {
                const size_t digit = alphabet.find(uri[i]);
                if (digit == std::string::npos)
                    throw std::runtime_error("invalid base64 glTF data URI");

                accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    bytes += static_cast<char>(accumulator >> bits);
                }
            }
            return bytes;
        }

        struct GltfAsset
        {
            rapidjson::Document json;
            std::filesystem::path directory;
            std::filesystem::path output;
            std::string stem;
            std::string binary;
            std::unordered_map<size_t, std::string> images;
            std::unordered_map<size_t, std::string> legacyImages;

            explicit GltfAsset(const std::filesystem::path &source, const std::filesystem::path &file)
                : directory(source.parent_path()), output(file.parent_path()), stem(Utf8(file.stem()))
            {
                std::string text = ReadBytes(source);
                if (text.size() >= 4 && text.compare(0, 4, "glTF") == 0)
                    text = ReadGlb(text);

                json.Parse(text.data(), text.size());
                if (json.HasParseError() || !json.IsObject())
                    throw std::runtime_error("invalid glTF JSON");
            }

            void MapLegacyImages(const ModelAsset &model, const std::vector<unsigned int> &materialIndices)
            {
                const auto &materials = Member(json, "materials");
                for (size_t i = 0; i < materialIndices.size(); ++i)
                {
                    if (!materials.IsArray() || materialIndices[i] >= materials.Size())
                        continue;

                    const auto &source = At(materials, materialIndices[i]);
                    const auto &pbr = Member(source, "pbrMetallicRoughness");
                    const std::array<const Json *, 5> slots = {
                        &Member(pbr, "baseColorTexture"), &Member(pbr, "metallicRoughnessTexture"),
                        &Member(source, "normalTexture"), &Member(source, "occlusionTexture"),
                        &Member(source, "emissiveTexture")};
                    const auto &material = *model.GetOwnedMaterials().at(i);
                    for (size_t slot = 0; slot < slots.size(); ++slot)
                    {
                        if (slots[slot]->IsNull() || !material.textures[slot])
                            continue;

                        const std::string &name = material.textures[slot]->GetResourceId();
                        if (name.empty() || name[0] != '*')
                            continue;

                        const auto &texture = At(Member(json, "textures"), Index(Member(*slots[slot], "index")));
                        const size_t index = Index(Member(texture, "source"));
                        if (legacyImages.contains(index))
                            continue;

                        std::vector<uint8_t> bytes;
                        std::string extension;
                        if (model.GetEmbeddedTextureBytes(name, bytes, extension))
                            legacyImages[index] = stem + "_embedded" + name.substr(1) + "." + extension;
                    }
                }
            }

            std::string ReadGlb(const std::string &bytes)
            {
                auto word = [&](size_t offset)
                {
                    uint32_t value;
                    if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
                        throw std::runtime_error("truncated GLB header");

                    std::memcpy(&value, bytes.data() + offset, sizeof(value));
                    return value;
                };
                if (word(4) != 2 || word(8) != bytes.size())
                    throw std::runtime_error("invalid GLB version or length");

                std::string text;
                for (size_t offset = 12; offset < bytes.size();)
                {
                    const uint32_t length = word(offset);
                    const uint32_t type = word(offset + 4);
                    offset += 8;
                    if (offset > bytes.size() || length > bytes.size() - offset)
                        throw std::runtime_error("truncated GLB chunk");

                    if (type == 0x4e4f534a)
                        text = bytes.substr(offset, length);
                    else if (type == 0x004e4942)
                        binary = bytes.substr(offset, length);

                    offset += length;
                }
                return text;
            }

            std::string Image(size_t index)
            {
                if (const auto it = images.find(index); it != images.end())
                    return it->second;

                const auto &image = At(Member(json, "images"), index);
                const std::string uri = String(Member(image, "uri"));
                std::string bytes;
                if (!uri.empty())
                    bytes = ReadUri(directory, uri);
                else
                    bytes = ImageView(At(Member(json, "bufferViews"), Index(Member(image, "bufferView"))));

                const auto relative = ImagePath(index, image, uri, bytes);
                const bool external = !uri.empty() && uri.rfind("data:", 0) != 0;
                std::error_code ec;
                const bool same =
                    external && std::filesystem::equivalent(directory / std::filesystem::u8path(DecodeUri(uri)),
                                                            output / relative, ec);
                if (!same)
                    WriteBytes(output / relative, bytes);

                return images[index] = Utf8(relative);
            }

            std::filesystem::path ImagePath(size_t index, const Json &image, const std::string &uri,
                                            const std::string &bytes) const
            {
                const std::string mime = String(Member(image, "mimeType"));
                const bool png = mime == "image/png" || (bytes.size() >= 4 && bytes.substr(1, 3) == "PNG");
                auto relative = std::filesystem::u8path(stem + "_images") /
                                ("image" + std::to_string(index) + (png ? ".png" : ".jpg"));
                if (const auto legacy = legacyImages.find(index); legacy != legacyImages.end())
                    relative = std::filesystem::u8path(legacy->second);

                const bool external = !uri.empty() && uri.rfind("data:", 0) != 0;
                if (external && !legacyImages.contains(index))
                {
                    const auto path = std::filesystem::u8path(DecodeUri(uri)).lexically_normal();
                    const bool portable =
                        !path.has_root_path() &&
                        std::none_of(path.begin(), path.end(), [](const auto &part)
                                     { return part == ".."; });
                    if (portable)
                        relative = path;
                }

                return relative;
            }

            std::string ImageView(const Json &view)
            {
                const auto &buffer = At(Member(json, "buffers"), Index(Member(view, "buffer")));
                const std::string uri = String(Member(buffer, "uri"));
                const std::string bytes = uri.empty() ? binary : ReadUri(directory, uri);
                const size_t offset = Member(view, "byteOffset").IsNull() ? 0 : Index(Member(view, "byteOffset"));
                const size_t length = Index(Member(view, "byteLength"));
                if (offset > bytes.size() || length > bytes.size() - offset)
                    throw std::runtime_error("glTF image buffer view out of bounds");

                return bytes.substr(offset, length);
            }
        };

        mx::InputPtr Connect(const mx::NodePtr &node, const std::string &input, const std::string &type,
                             const mx::NodePtr &upstream, const std::string &output = {})
        {
            const auto port = node->addInput(input, type);
            port->setNodeName(upstream->getName());
            if (!output.empty())
                port->setOutputString(output);

            return port;
        }

        void CopyInput(const mx::NodePtr &node, const std::string &name, const mx::InputPtr &source)
        {
            node->addInput(name, source->getType())->copyContentFrom(source);
        }

        std::pair<float, float> AnisotropyParameters(float roughness, float strength)
        {
            const float ab = std::max(roughness * roughness, 1e-5f);
            const float at = std::max(roughness * roughness * (1 - strength * strength) + strength * strength, 1e-5f);
            return {std::pow((at * at + ab * ab) * 0.5f, 0.25f), 1 - ab / at};
        }

        struct MaterialGraph
        {
            GltfAsset &asset;
            mx::DocumentPtr document = mx::createDocument();
            mx::NodePtr surface = document->addNode("open_pbr_surface", "surface", "surfaceshader");

            explicit MaterialGraph(GltfAsset &source) : asset(source)
            {
                document->setColorSpace("lin_rec709");
                const auto material = document->addNode("surfacematerial", "Material", "material");
                Connect(material, "surfaceshader", "surfaceshader", surface);
            }

            mx::NodePtr Texture(const Json &info, const std::string &name, const std::string &category,
                                const std::string &type, bool srgb = false)
            {
                const auto &texture = At(Member(asset.json, "textures"), Index(Member(info, "index")));
                const auto node = document->addNode(category, name, type);
                node->setInputValue("file", asset.Image(Index(Member(texture, "source"))), "filename");
                if (srgb)
                    node->getInput("file")->setColorSpace("srgb_texture");

                const auto &transform = Member(Member(info, "extensions"), "KHR_texture_transform");
                const auto &uv =
                    Member(transform, "texCoord").IsNull() ? Member(info, "texCoord") : Member(transform, "texCoord");
                if (!uv.IsNull() && Index(uv) != 0)
                    throw std::runtime_error("OpenPBR import currently requires TEXCOORD_0");

                SetTransform(node, transform);
                SetSampler(node, Member(texture, "sampler"));
                return node;
            }

            void SetTransform(const mx::NodePtr &node, const Json &transform)
            {
                for (const char *name : {"scale", "offset"})
                {
                    const auto &value = Member(transform, name);
                    if (!value.IsNull())
                    {
                        const auto x = Number(At(value, 0), 0);
                        const auto y = Number(At(value, 1), 0);
                        node->setInputValue(name, mx::Vector2(x, y));
                    }
                }

                node->setInputValue("rotate", Number(Member(transform, "rotation"), 0) * 180.0f / glm::pi<float>());
            }

            void SetSampler(const mx::NodePtr &node, const Json &samplerIndex)
            {
                static const Json missing;
                const auto &sampler =
                    samplerIndex.IsNull() ? missing : At(Member(asset.json, "samplers"), Index(samplerIndex));
                for (const auto &[key, input] : {std::pair{"wrapS", "uaddressmode"}, {"wrapT", "vaddressmode"}})
                {
                    const int mode = static_cast<int>(Number(Member(sampler, key), 10497));
                    const std::string address = mode == 33071 ? "clamp" : mode == 33648 ? "mirror"
                                                                                        : "periodic";
                    node->setInputValue(input, address);
                }

                const int mag = static_cast<int>(Number(Member(sampler, "magFilter"), 9729));
                node->setInputValue("filtertype", std::string(mag == 9728 ? "closest" : "linear"));
            }

            mx::InputPtr Scalar(const std::string &name, float factor, const Json &info, int channel = 0)
            {
                if (info.IsNull())
                {
                    surface->setInputValue(name, factor);
                    return surface->getInput(name);
                }

                const auto image = Texture(info, name + "_image", "gltf_image", channel == 3 ? "color4" : "vector3");
                const auto extract = document->addNode("extract", name + "_channel", "float");
                Connect(extract, "in", image->getType(), image);
                extract->setInputValue("index", channel);

                const auto multiply = document->addNode("multiply", name + "_factor", "float");
                Connect(multiply, "in1", "float", extract);
                multiply->setInputValue("in2", factor);
                return Connect(surface, name, "float", multiply);
            }

            void ColorInput(const std::string &name, const mx::Color3 &factor, const Json &info)
            {
                if (info.IsNull())
                {
                    surface->setInputValue(name, factor);
                    return;
                }

                const auto image = Texture(info, name + "_image", "gltf_image", "color3", true);
                image->setInputValue("factor", factor);
                Connect(surface, name, "color3", image);
            }

            void Base(const Json &material)
            {
                const auto &pbr = Member(material, "pbrMetallicRoughness");
                const auto &rgba = Member(pbr, "baseColorFactor");
                const auto color = Color(rgba, mx::Color3(1));
                const auto &info = Member(pbr, "baseColorTexture");
                if (info.IsNull())
                    surface->setInputValue("base_color", color);
                else
                    BaseTexture(info, color, rgba.IsNull() ? 1 : Number(At(rgba, 3), 1));

                Scalar("base_metalness", Number(Member(pbr, "metallicFactor"), 1),
                       Member(pbr, "metallicRoughnessTexture"), 2);
                Scalar("specular_roughness", Number(Member(pbr, "roughnessFactor"), 1),
                       Member(pbr, "metallicRoughnessTexture"), 1);
                const auto &ior = Member(Member(material, "extensions"), "KHR_materials_ior");
                surface->setInputValue("specular_ior", Number(Member(ior, "ior"), 1.5f));
            }

            void BaseTexture(const Json &info, const mx::Color3 &factor, float alpha)
            {
                // gltf_colorimage hardcodes periodic addressing internally; the plain gltf_image honours samplers.
                const auto &texture = At(Member(asset.json, "textures"), Index(Member(info, "index")));
                const auto &samplerIndex = Member(texture, "sampler");
                static const Json missing;
                const auto &sampler =
                    samplerIndex.IsNull() ? missing : At(Member(asset.json, "samplers"), Index(samplerIndex));
                const bool periodic = Number(Member(sampler, "wrapS"), 10497) == 10497 &&
                                      Number(Member(sampler, "wrapT"), 10497) == 10497;
                if (periodic)
                {
                    const auto image = Texture(info, "base_color_image", "gltf_colorimage", "multioutput", true);
                    image->setInputValue("color", mx::Color4(factor[0], factor[1], factor[2], alpha));
                    Connect(surface, "base_color", "color3", image, "outcolor");
                }
                else
                    ColorInput("base_color", factor, info);
            }

            void Coat(const Json &extension)
            {
                Scalar("coat_weight", Number(Member(extension, "clearcoatFactor"), 0),
                       Member(extension, "clearcoatTexture"));
                Scalar("coat_roughness", Number(Member(extension, "clearcoatRoughnessFactor"), 0),
                       Member(extension, "clearcoatRoughnessTexture"), 1);
                surface->setInputValue("coat_ior", 1.5f);
                surface->setInputValue("coat_darkening", 0.0f);
                Normal("geometry_coat_normal", Member(extension, "clearcoatNormalTexture"));
            }

            void Normal(const std::string &name, const Json &info)
            {
                if (info.IsNull())
                    return;

                const float scale = Number(Member(info, "scale"), 1);
                if (scale == 1)
                {
                    Connect(surface, name, "vector3", Texture(info, name + "_image", "gltf_normalmap", "vector3"));
                    return;
                }

                const auto image = Texture(info, name + "_image", "gltf_image", "vector3");
                const auto normal = document->addNode("normalmap", name + "_scale", "vector3");
                Connect(normal, "in", "vector3", image);
                normal->setInputValue("scale", scale);
                Connect(surface, name, "vector3", normal);
            }

            void Sheen(const Json &extension)
            {
                const auto color = Color(Member(extension, "sheenColorFactor"), mx::Color3(0));
                const float maximum = std::max({color[0], color[1], color[2]});
                const auto &info = Member(extension, "sheenColorTexture");
                if (info.IsNull())
                {
                    surface->setInputValue("fuzz_weight", maximum);
                    surface->setInputValue("fuzz_color", maximum > 0 ? color / maximum : mx::Color3(1));
                }
                else
                    SheenTexture(color, info);

                Scalar("fuzz_roughness", Number(Member(extension, "sheenRoughnessFactor"), 0),
                       Member(extension, "sheenRoughnessTexture"), 3);
            }

            void SheenTexture(const mx::Color3 &factor, const Json &info)
            {
                const auto image = Texture(info, "fuzz_color_image", "gltf_image", "color3", true);
                image->setInputValue("factor", factor);
                std::array<mx::NodePtr, 3> channel;
                for (int i = 0; i < 3; ++i)
                {
                    channel[i] = document->addNode("extract", "fuzz_color_" + std::to_string(i), "float");
                    Connect(channel[i], "in", "color3", image);
                    channel[i]->setInputValue("index", i);
                }

                const auto rg = document->addNode("max", "fuzz_max_rg", "float");
                Connect(rg, "in1", "float", channel[0]);
                Connect(rg, "in2", "float", channel[1]);
                const auto rgb = document->addNode("max", "fuzz_max_rgb", "float");
                Connect(rgb, "in1", "float", rg);
                Connect(rgb, "in2", "float", channel[2]);
                Connect(surface, "fuzz_weight", "float", rgb);

                const auto divisor = document->addNode("max", "fuzz_divisor", "float");
                Connect(divisor, "in1", "float", rgb);
                divisor->setInputValue("in2", 1e-8f);
                const auto normalized = document->addNode("divide", "fuzz_normalized", "color3");
                Connect(normalized, "in1", "color3", image);
                Connect(normalized, "in2", "float", divisor);
                Connect(surface, "fuzz_color", "color3", normalized);
            }

            void SpecularAndEmission(const Json &material)
            {
                const auto &extensions = Member(material, "extensions");
                const auto &specular = Member(extensions, "KHR_materials_specular");
                Scalar("specular_weight", Number(Member(specular, "specularFactor"), 1),
                       Member(specular, "specularTexture"), 3);
                ColorInput("specular_color", Color(Member(specular, "specularColorFactor"), mx::Color3(1)),
                           Member(specular, "specularColorTexture"));

                ColorInput("emission_color", Color(Member(material, "emissiveFactor"), mx::Color3(0)),
                           Member(material, "emissiveTexture"));
                const auto &strength = Member(extensions, "KHR_materials_emissive_strength");
                surface->setInputValue("emission_luminance", Number(Member(strength, "emissiveStrength"), 1));
            }

            void Transmission(const Json &extensions)
            {
                const auto &transmission = Member(extensions, "KHR_materials_transmission");
                Scalar("transmission_weight", Number(Member(transmission, "transmissionFactor"), 0),
                       Member(transmission, "transmissionTexture"));

                const auto &volume = Member(extensions, "KHR_materials_volume");
                const float distance = Number(Member(volume, "attenuationDistance"), 0);
                const auto color =
                    distance > 0 ? Color(Member(volume, "attenuationColor"), mx::Color3(1)) : mx::Color3(1);
                surface->setInputValue("transmission_color", color);
                surface->setInputValue("transmission_depth", distance);
                surface->setInputValue("geometry_thin_walled", Number(Member(volume, "thicknessFactor"), 0) == 0);
            }

            void Iridescence(const Json &extension)
            {
                Scalar("thin_film_weight", Number(Member(extension, "iridescenceFactor"), 0),
                       Member(extension, "iridescenceTexture"));
                surface->setInputValue("thin_film_ior", Number(Member(extension, "iridescenceIor"), 1.3f));
                const float maximum = Number(Member(extension, "iridescenceThicknessMaximum"), 400);
                const auto &info = Member(extension, "iridescenceThicknessTexture");
                if (info.IsNull())
                    surface->setInputValue("thin_film_thickness",
                                           maximum * 0.001f); // glTF nanometres -> OpenPBR micrometres
                else
                {
                    const auto image =
                        Texture(info, "thin_film_thickness_image", "gltf_iridescence_thickness", "float");
                    image->setInputValue("thicknessMin", Number(Member(extension, "iridescenceThicknessMinimum"), 100));
                    image->setInputValue("thicknessMax", maximum);
                    const auto units = document->addNode("multiply", "thin_film_thickness_units", "float");
                    Connect(units, "in1", "float", image);
                    units->setInputValue("in2", 0.001f);
                    Connect(surface, "thin_film_thickness", "float", units);
                }
            }

            mx::NodePtr Constant(const std::string &name, float value)
            {
                const auto node = document->addNode("constant", name, "float");
                node->setInputValue("value", value);
                return node;
            }

            mx::NodePtr Binary(const std::string &category, const std::string &name, const mx::NodePtr &a,
                               const mx::NodePtr &b)
            {
                const auto node = document->addNode(category, name, "float");
                Connect(node, "in1", "float", a);
                Connect(node, "in2", "float", b);
                return node;
            }

            void Anisotropy(const Json &extension)
            {
                if (extension.IsNull())
                    return;

                const float strength = Number(Member(extension, "anisotropyStrength"), 0);
                const float rotation = Number(Member(extension, "anisotropyRotation"), 0);
                const auto &info = Member(extension, "anisotropyTexture");
                const auto roughness = surface->getInput("specular_roughness");
                if (info.IsNull() && roughness->hasValueString())
                {
                    const float r = roughness->getValue()->asA<float>();
                    const auto [mappedRoughness, mappedStrength] = AnisotropyParameters(r, strength);
                    surface->setInputValue("specular_roughness", mappedRoughness);
                    surface->setInputValue("specular_roughness_anisotropy", mappedStrength);
                    AnisotropyTangent(Constant("anisotropy_rotation", rotation));
                    return;
                }

                auto s = Constant("anisotropy_strength", strength);
                auto angle = Constant("anisotropy_rotation", rotation);
                if (!info.IsNull())
                {
                    const auto image = Texture(info, "anisotropy_image", "gltf_anisotropy_image", "multioutput");
                    image->setInputValue("anisotropy_strength", strength);
                    image->setInputValue("anisotropy_rotation", rotation);
                    document->removeNode(s->getName());
                    document->removeNode(angle->getName());
                    s = document->addNode("dot", "anisotropy_strength", "float");
                    Connect(s, "in", "float", image, "anisotropy_strength_out");
                    angle = document->addNode("dot", "anisotropy_rotation", "float");
                    Connect(angle, "in", "float", image, "anisotropy_rotation_out");
                }

                AnisotropyRoughness(s, roughness);
                AnisotropyTangent(angle);
            }

            void AnisotropyRoughness(const mx::NodePtr &strength, const mx::InputPtr &roughness)
            {
                // Match glTF's two GGX alpha axes pointwise, including textured roughness/strength.
                const auto r = document->addNode("dot", "anisotropy_roughness", "float");
                CopyInput(r, "in", roughness);
                const auto r2 = Binary("multiply", "anisotropy_r2", r, r);
                const auto s2 = Binary("multiply", "anisotropy_s2", strength, strength);
                const auto one = Constant("anisotropy_one", 1);
                const auto oneMinus = Binary("subtract", "anisotropy_one_minus_s2", one, s2);
                const auto product = Binary("multiply", "anisotropy_product", r2, oneMinus);
                const auto rawAt = Binary("add", "anisotropy_raw_at", product, s2);
                const auto minimum = Constant("anisotropy_minimum", 1e-5f);
                const auto at = Binary("max", "anisotropy_at", rawAt, minimum);
                const auto ab = Binary("max", "anisotropy_ab", r2, minimum);
                const auto at2 = Binary("multiply", "anisotropy_at2", at, at);
                const auto ab2 = Binary("multiply", "anisotropy_ab2", ab, ab);
                const auto sum = Binary("add", "anisotropy_sum", at2, ab2);
                const auto mean = Binary("multiply", "anisotropy_mean", sum, Constant("anisotropy_half", 0.5f));
                const auto mappedRoughness =
                    Binary("power", "anisotropy_mapped_roughness", mean, Constant("anisotropy_quarter", 0.25f));
                const auto ratio = Binary("divide", "anisotropy_ratio", ab, at);
                const auto mappedStrength = Binary("subtract", "anisotropy_mapped_strength", one, ratio);
                surface->removeInput("specular_roughness");
                Connect(surface, "specular_roughness", "float", mappedRoughness);
                Connect(surface, "specular_roughness_anisotropy", "float", mappedStrength);
            }

            void AnisotropyTangent(const mx::NodePtr &rotation)
            {
                const auto degrees = Binary("multiply", "anisotropy_degrees", rotation,
                                            Constant("anisotropy_radians_to_degrees", -180.0f / glm::pi<float>()));
                const auto tangent = document->addNode("tangent", "anisotropy_tangent", "vector3");
                tangent->setInputValue("space", std::string("world"));
                const auto normal = document->addNode("normal", "anisotropy_normal", "vector3");
                normal->setInputValue("space", std::string("world"));
                const auto rotated = document->addNode("rotate3d", "anisotropy_rotated_tangent", "vector3");
                Connect(rotated, "in", "vector3", tangent);
                Connect(rotated, "axis", "vector3", normal);
                Connect(rotated, "amount", "float", degrees);
                Connect(surface, "geometry_tangent", "vector3", rotated);
            }

            void Build(const Json &material)
            {
                const auto &extensions = Member(material, "extensions");
                Base(material);
                Coat(Member(extensions, "KHR_materials_clearcoat"));
                Sheen(Member(extensions, "KHR_materials_sheen"));
                SpecularAndEmission(material);
                Transmission(extensions);
                Iridescence(Member(extensions, "KHR_materials_iridescence"));
                Normal("geometry_normal", Member(material, "normalTexture"));
                Anisotropy(Member(extensions, "KHR_materials_anisotropy"));
            }
        };

        void MirrorLegacy(Material &material, const Json &source)
        {
            const auto &pbr = Member(source, "pbrMetallicRoughness");
            const auto &rgba = Member(pbr, "baseColorFactor");
            const auto color = Color(rgba, mx::Color3(1));
            material.baseColorFactor = vec4(glm::convertLinearToSRGB(vec3(color[0], color[1], color[2])),
                                            rgba.IsNull() ? 1 : Number(At(rgba, 3), 1));
            material.metallic = Number(Member(pbr, "metallicFactor"), 1);
            material.roughness = Number(Member(pbr, "roughnessFactor"), 1);
            material.normalScale = Member(source, "normalTexture").IsNull()
                                       ? 0
                                       : Number(Member(Member(source, "normalTexture"), "scale"), 1);
            material.occlusionStrength = Member(source, "occlusionTexture").IsNull()
                                             ? 0
                                             : Number(Member(Member(source, "occlusionTexture"), "strength"), 1);
            const auto &extensions = Member(source, "extensions");
            const auto &anisotropy = Member(extensions, "KHR_materials_anisotropy");
            const bool scalarAnisotropy = !anisotropy.IsNull() &&
                                          Member(anisotropy, "anisotropyTexture").IsNull() &&
                                          Member(pbr, "metallicRoughnessTexture").IsNull();
            if (scalarAnisotropy)
            {
                material.roughness = AnisotropyParameters(material.roughness,
                                                          Number(Member(anisotropy, "anisotropyStrength"), 0))
                                         .first;
            }

            material.ior = Number(Member(Member(extensions, "KHR_materials_ior"), "ior"), 1.5f);

            const auto emission = Color(Member(source, "emissiveFactor"), mx::Color3(0));
            const float strength =
                Number(Member(Member(extensions, "KHR_materials_emissive_strength"), "emissiveStrength"), 1);
            const vec3 radiance = vec3(emission[0], emission[1], emission[2]) * strength;
            const float peak = std::max({radiance.r, radiance.g, radiance.b, 1.0f});
            material.emissiveFactor = glm::convertLinearToSRGB(radiance / peak) * peak;

            const auto &transmission = Member(extensions, "KHR_materials_transmission");
            material.transmissionFactor = Number(Member(transmission, "transmissionFactor"), 0);
            const auto &volume = Member(extensions, "KHR_materials_volume");
            material.thicknessFactor = Number(Member(volume, "thicknessFactor"), 0);
            material.attenuationDistance =
                Number(Member(volume, "attenuationDistance"), std::numeric_limits<float>::infinity());
            const auto attenuation = Color(Member(volume, "attenuationColor"), mx::Color3(1));
            material.attenuationColor = vec3(attenuation[0], attenuation[1], attenuation[2]);
            material.SyncParamsFromLegacy();
        }

        bool LegacyOnly(const Json &material)
        {
            const auto &extensions = Member(material, "extensions");
            const bool blended = String(Member(material, "alphaMode"), "OPAQUE") == "BLEND";
            const bool unsupported = !Member(extensions, "KHR_materials_unlit").IsNull() ||
                                     !Member(extensions, "KHR_materials_diffuse_transmission").IsNull() ||
                                     !Member(extensions, "KHR_materials_pbrSpecularGlossiness").IsNull();
            return blended || unsupported;
        }
    } // namespace

    bool PrepareGltfMaterialImport(const std::filesystem::path &file, std::string &bytes,
                                   std::vector<std::string> &materialNames, std::string &error)
    {
        try
        {
            GltfAsset asset(file, file);
            auto materials = asset.json.FindMember("materials");
            if (materials != asset.json.MemberEnd())
            {
                if (!materials->value.IsArray())
                    throw std::runtime_error("glTF materials must be an array");

                for (auto &material : materials->value.GetArray())
                {
                    if (!material.IsObject())
                        throw std::runtime_error("glTF material must be an object");

                    materialNames.push_back(String(Member(material, "name")));
                    const std::string tag = "__pe_gltf_material_" + std::to_string(materialNames.size() - 1);
                    material.RemoveMember("name");
                    material.AddMember("name", Json(tag.c_str(), asset.json.GetAllocator()), asset.json.GetAllocator());
                }
            }

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            asset.json.Accept(writer);
            bytes.assign(buffer.GetString(), buffer.GetSize());
            std::string extension = file.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (extension == ".glb")
            {
                bytes.append((4 - bytes.size() % 4) % 4, ' ');
                const uint32_t header[] = {0x46546c67, 2,
                                           static_cast<uint32_t>(bytes.size() + asset.binary.size() + 28),
                                           static_cast<uint32_t>(bytes.size()), 0x4e4f534a};
                const uint32_t binaryHeader[] = {static_cast<uint32_t>(asset.binary.size()), 0x004e4942};
                bytes.insert(0, reinterpret_cast<const char *>(header), sizeof(header));
                bytes.append(reinterpret_cast<const char *>(binaryHeader), sizeof(binaryHeader));
                bytes += asset.binary;
            }
            return true;
        }
        catch (const std::exception &failure)
        {
            error = failure.what();
            return false;
        }
    }

    bool WriteGltfMaterialX(const ModelAsset &model, const std::vector<unsigned int> &materialIndices,
                            const std::filesystem::path &file,
                            std::vector<std::pair<std::string, std::string>> &references, std::string &error)
    {
        try
        {
            GltfAsset asset(model.GetFilePath(), file);
            asset.MapLegacyImages(model, materialIndices);
            const auto &materials = Member(asset.json, "materials");
            const size_t count = materials.IsNull() ? 0 : materials.Size();
            const bool hasDefault =
                std::find(materialIndices.begin(), materialIndices.end(), count) != materialIndices.end();
            std::vector<std::pair<std::string, std::string>> documents(count + (hasDefault ? 1 : 0));
            const auto libraries = mx::createDocument();
            mx::loadLibraries({"libraries"},
                              mx::FileSearchPath(
                                  mx::FilePath((std::filesystem::path(Path::ExecutablePath()) / "MaterialX").string())),
                              libraries);

            static const Json defaultMaterial;
            for (size_t i = 0; i < documents.size(); ++i)
            {
                const auto &material = i < count ? At(materials, i) : defaultMaterial;
                if (LegacyOnly(material))
                    continue;

                MaterialGraph graph(asset);
                graph.Build(material);
                graph.document->setDataLibrary(libraries);
                std::string validation;
                if (!graph.document->validate(&validation))
                    throw std::runtime_error(validation);

                const std::string name = asset.stem + ".material" + std::to_string(i) + ".mtlx";
                WriteBytes(asset.output / std::filesystem::u8path(name), mx::writeToXmlString(graph.document));
                documents[i] = {name, "Material"};
            }

            references.resize(materialIndices.size());
            for (size_t i = 0; i < materialIndices.size(); ++i)
            {
                references[i] = documents.at(materialIndices[i]);
                const auto &material = materialIndices[i] < count ? At(materials, materialIndices[i]) : defaultMaterial;
                if (!references[i].first.empty())
                    MirrorLegacy(*model.GetOwnedMaterials().at(i), material);
            }
            return true;
        }
        catch (const std::exception &failure)
        {
            error = failure.what();
            return false;
        }
    }
} // namespace pe
