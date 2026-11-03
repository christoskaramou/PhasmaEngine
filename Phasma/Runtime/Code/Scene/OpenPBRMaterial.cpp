#include "Scene/OpenPBRMaterial.h"
#include "Scene/OpenPBRShapes.h"
#include "Base/Process.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "gtc/color_space.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "stb_image.h"
#include <cmath>
#include <fstream>

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

        std::string ReadOpenPBRFile(const std::string &path)
        {
            FileSystem file(path, std::ios::in | std::ios::ate);
            if (!AssetFileExists(path) || !file.IsOpen())
                throw std::runtime_error("cannot load " + path);

            return file.ReadAll();
        }

        uint32_t InputWidth(const std::string &type)
        {
            const bool integer = type == "bool" || type == "int" || type == "uint";
            if (integer || type == "float")
                return 1;
            if (type == "float2" || type == "SamplerTexture2D")
                return 2;
            if (type == "float3")
                return 3;
            if (type == "float4")
                return 4;

            throw std::runtime_error("unsupported input type '" + type + "' (matrix/string inputs are unavailable)");
        }

        MaterialParamValue ReadDefault(const rapidjson::Value &values, const std::string &type)
        {
            const uint32_t width = InputWidth(type);
            if (!values.IsArray() || values.Size() != width)
                throw std::runtime_error("input default count does not match its type");

            if (type == "bool")
            {
                if (!values[0].IsUint() || values[0].GetUint() > 1u)
                    throw std::runtime_error("boolean default must be 0 or 1");

                return values[0].GetUint();
            }

            if (type == "int")
            {
                if (!values[0].IsInt())
                    throw std::runtime_error("integer default is outside int32");

                return values[0].GetInt();
            }

            if (type == "uint")
            {
                if (!values[0].IsUint())
                    throw std::runtime_error("integer default is outside uint32");

                return values[0].GetUint();
            }

            float v[4]{};
            for (uint32_t i = 0; i < width; ++i)
            {
                if (!values[i].IsNumber() || std::abs(values[i].GetDouble()) > std::numeric_limits<float>::max())
                    throw std::runtime_error("input default is not a finite float");

                v[i] = values[i].GetFloat();
            }

            if (width == 2)
                return vec2(v[0], v[1]);
            if (width == 3)
                return vec3(v[0], v[1], v[2]);
            if (width == 4)
                return vec4(v[0], v[1], v[2], v[3]);

            return v[0];
        }

        bool HasString(const rapidjson::Value &object, const char *key)
        {
            return object.IsObject() && object.HasMember(key) && object[key].IsString();
        }

        OpenPBRInput ReadInput(const rapidjson::Value &item, uint32_t byteSize)
        {
            const bool names = HasString(item, "node") && HasString(item, "input") && HasString(item, "name");
            const bool storage = HasString(item, "type") && item.HasMember("offset") && item["offset"].IsUint();
            const bool values = item.IsObject() && item.HasMember("default");
            if (!names || !storage || !values)
                throw std::runtime_error("malformed input entry");

            OpenPBRInput input;
            input.node = item["node"].GetString();
            input.input = item["input"].GetString();
            input.identity = input.node + "." + input.input;
            input.type = item["type"].GetString();
            input.offset = item["offset"].GetUint();

            const uint32_t bytes = InputWidth(input.type) * sizeof(float);
            const bool inBounds = input.offset <= byteSize && bytes <= byteSize - input.offset;
            const bool named = !input.node.empty() && !input.input.empty();
            if (!named || input.offset % 4u != 0u || !inBounds)
                throw std::runtime_error("input '" + input.identity + "' is unaligned or outside its byte layout");

            return input;
        }

        rapidjson::Document ReadLayoutDocument(const std::string &json, const std::string &code)
        {
            rapidjson::Document doc;
            doc.Parse(json.c_str());
            if (doc.HasParseError() || !doc.IsObject())
                throw std::runtime_error("not a PhasmaCook material layout");

            const bool abi = doc.HasMember("module_abi") && doc["module_abi"].IsUint() &&
                             doc["module_abi"].GetUint() == kOpenPBRModuleAbi;
            const std::string marker = "#define PE_OPENPBR_MODULE_ABI " + std::to_string(kOpenPBRModuleAbi) + "\n";
            if (!abi || !code.starts_with(marker))
            {
                throw std::runtime_error("incompatible OpenPBR module ABI; regenerate the material with the matching "
                                         "PhasmaCook/editor and re-export cooked content");
            }

            const bool inputs = doc.HasMember("inputs") && doc["inputs"].IsArray();
            const bool storage = doc.HasMember("byteSize") && doc["byteSize"].IsUint();
            const bool thin = doc.HasMember("thin") && doc["thin"].IsBool();
            const bool header = inputs && storage && thin;
            if (!header || !HasString(doc, "surface") || doc["surface"].GetStringLength() == 0)
                throw std::runtime_error("not a PhasmaCook material layout");

            if (code.find("PeThin pe_material_thin(") == std::string::npos)
                throw std::runtime_error("generated module has no thin helper");

            return doc;
        }

        OpenPBRLayout ReadLayout(const std::string &json,
                                 const std::string &code,
                                 OpenPBRMaterialSource &source)
        {
            const auto doc = ReadLayoutDocument(json, code);
            OpenPBRLayout layout;
            layout.byteSize = doc["byteSize"].GetUint();
            layout.surfaceNode = doc["surface"].GetString();
            if (layout.byteSize > UINT32_MAX - kOpenPBRInputsOffset || layout.byteSize % 4u != 0u)
                throw std::runtime_error("material byteSize overflows its record or is unaligned");

            uint64_t packedBytes = 0;
            for (const auto &item : doc["inputs"].GetArray())
            {
                OpenPBRInput input = ReadInput(item, layout.byteSize);
                const uint32_t bytes = InputWidth(input.type) * sizeof(float);
                for (const OpenPBRInput &other : layout.inputs)
                {
                    const uint32_t otherBytes = InputWidth(other.type) * sizeof(float);
                    if (input.offset < other.offset + otherBytes && other.offset < input.offset + bytes)
                        throw std::runtime_error("overlapping input '" + input.identity + "'");
                }

                if (source.defaults.contains(input.identity) || source.textures.contains(input.identity))
                    throw std::runtime_error("duplicate input '" + input.identity + "'");

                if (input.type == "SamplerTexture2D")
                {
                    const bool emptyDefault = item["default"].IsArray() && item["default"].Empty();
                    if (!emptyDefault || !HasString(item, "file") || item["file"].GetStringLength() == 0)
                        throw std::runtime_error("texture '" + input.identity + "' has no resolved file");

                    const std::string file = item["file"].GetString();
                    const std::string resolved = PathFromUtf8(file).is_absolute() ? file : Path::ResolveAsset(file);
                    source.textures.emplace(input.identity, OpenPBRTexture{resolved, {}});
                }
                else
                {
                    source.defaults.emplace(input.identity, ReadDefault(item["default"], input.type));
                }

                packedBytes += bytes;
                layout.inputs.push_back(std::move(input));
            }

            if (packedBytes != layout.byteSize || layout.surfaceNode.empty())
                throw std::runtime_error("layout is not tightly packed or has no OpenPBR surface");

            layout.valid = true;
            return layout;
        }

        void CheckDefaultLayout(const OpenPBRLayout &layout)
        {
            if (layout.byteSize != kOpenPBRDefaultBytes || layout.inputs.size() != std::size(kOpenPBRDefaultSchema))
                throw std::runtime_error("default module must have 37 inputs / 220 bytes / thin:true");

            for (size_t i = 0; i < layout.inputs.size(); ++i)
            {
                const auto &input = layout.inputs[i];
                const auto &expected = kOpenPBRDefaultSchema[i];
                if (input.input != expected.input || input.type != expected.type || input.offset != expected.offset)
                    throw std::runtime_error("default module input '" + input.identity + "' differs from the pinned schema");
            }
        }

        std::string AbsoluteShaderPath(const std::filesystem::path &path)
        {
            std::string result = PathUtf8(std::filesystem::absolute(path).lexically_normal());
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }

        void WriteOpenPBRFile(const std::string &path, const std::string &text)
        {
            std::ofstream file(PathFromUtf8(path), std::ios::binary);
            file << text;
            file.close();
            if (!file)
                throw std::runtime_error("cannot write " + path);
        }

        void WriteWrapper(const std::string &path, const std::string &module, const char *pass)
        {
            const std::string dir = Path::RuntimeAssetsPath() + "Shaders/MaterialX/";
            const std::string stem = dir + pass;
            const std::string code = "#include \"" + AbsoluteShaderPath(stem + "Head.slang") + "\"\n" +
                                     "#include \"" + AbsoluteShaderPath(module) + "\"\n" +
                                     "#include \"" + AbsoluteShaderPath(stem + "Main.slang") + "\"\n";

            WriteOpenPBRFile(path, code);
        }

        struct SourceResult
        {
            std::shared_ptr<const OpenPBRMaterialSource> source;
            std::string error;
        };

        struct OpenPBRRegistry
        {
            std::vector<std::shared_ptr<const OpenPBRModule>> modules;
            std::shared_ptr<const OpenPBRMaterialSource> defaultSource;
            std::unordered_map<std::string, SourceResult> sources;
        };

        OpenPBRRegistry &Registry()
        {
            static OpenPBRRegistry registry = []
            {
                OpenPBRRegistry result;
                auto module = std::make_shared<OpenPBRModule>();
                auto source = std::make_shared<OpenPBRMaterialSource>();
                source->material = "PeOpenPBRDefault";
                source->module = module;
                const std::string dir = Path::RuntimeAssetsPath() + "Shaders/MaterialX/";
                source->path = dir + "Generated/PeOpenPBRDefault.json";
                module->document = source->path;

                try
                {
                    const std::string code = ReadOpenPBRFile(dir + "Generated/PeOpenPBRDefault.slang");
                    module->layout = ReadLayout(ReadOpenPBRFile(source->path), code, *source);
                    CheckDefaultLayout(module->layout);
                    source->layout = module->layout;
                    module->hash = std::to_string(Fnv1a64(code));
                    module->code = dir + "Generated/PeOpenPBRDefault.slang";
                }
                catch (const std::exception &error)
                {
                    module->layout.valid = false;
                    PE_WARN("[OpenPBR] %s: %s; materials keep their legacy shading", source->path.c_str(), error.what());
                }

                result.modules.push_back(module);
                result.defaultSource = source;
                return result;
            }();

            return registry;
        }

        // Same-shape graphs share code under their own names (OpenPBRMaterialSource::layout): only the bytes must agree.
        void CheckSharedLayout(const OpenPBRLayout &a, const OpenPBRLayout &b)
        {
            if (a.byteSize != b.byteSize || a.inputs.size() != b.inputs.size())
                throw std::runtime_error("identical module code has a different byte layout");

            for (size_t i = 0; i < a.inputs.size(); ++i)
            {
                const auto &x = a.inputs[i];
                const auto &y = b.inputs[i];
                if (x.type != y.type || x.offset != y.offset)
                    throw std::runtime_error("identical module code has a different input layout");
            }
        }

        std::shared_ptr<const OpenPBRModule> RegisterModule(const std::string &document,
                                                            const std::string &modulePath,
                                                            const std::string &code,
                                                            OpenPBRLayout layout)
        {
            OpenPBRRegistry &registry = Registry();
            const std::string hash = std::to_string(Fnv1a64(code));
            for (const auto &module : registry.modules)
            {
                if (module->hash != hash)
                    continue;

                CheckSharedLayout(module->layout, layout);
                return module;
            }

            auto module = std::make_shared<OpenPBRModule>();
            module->hash = hash;
            module->index = static_cast<uint32_t>(registry.modules.size());
            module->document = document;
            module->layout = std::move(layout);
            module->code = modulePath;

            registry.modules.push_back(module);
            return module;
        }

        // The session's shape order: each project's list is read when first used, then shapes are only appended.
        struct ShapeList
        {
            std::vector<std::string> hashes;
            std::unordered_map<std::string, uint32_t> index;
            std::unordered_set<std::string> seeded;                      // assets paths whose list was read
            std::unordered_set<std::string> broken;                      // group keys whose combined compile failed
            std::unordered_map<std::string, OpenPBRPassSources> sources; // by member list
        };

        ShapeList &Shapes()
        {
            static ShapeList shapes;
            return shapes;
        }

        std::string CookedModuleDir()
        {
            return Path::AssetsPath() + "MaterialX/Cooked/";
        }

        std::string ShapeListPath()
        {
            return CookedModuleDir() + "shapes_abi" + std::to_string(kOpenPBRModuleAbi) + ".txt";
        }

        // Shapes inline into the entry function, so compile time grows with the group (DX12 shade, 8 shapes: 5.6 s,
        // 54: 34.6 s; 2026-10-29).
        uint32_t ShapeGroupSize()
        {
            return 8u;
        }

        bool IsCookedModule(const std::string &code)
        {
            return code.starts_with("#define PE_OPENPBR_MODULE_ABI " + std::to_string(kOpenPBRModuleAbi) + "\n");
        }

        // The project's list; a project without one starts from its cooked modules of this ABI, in hash order.
        void SeedShapes(ShapeList &shapes)
        {
            if (!shapes.seeded.insert(Path::AssetsPath()).second)
                return;

            std::vector<std::string> hashes;
            std::ifstream file(PathFromUtf8(ShapeListPath()));
            for (std::string line; std::getline(file, line);)
            {
                if (!line.empty())
                    hashes.push_back(line);
            }

            if (!file.is_open())
            {
                std::error_code ec;
                for (const auto &entry : std::filesystem::directory_iterator(PathFromUtf8(CookedModuleDir()), ec))
                {
                    const std::string stem = PathUtf8(entry.path().stem());
                    const bool named = entry.path().extension() == ".slang" &&
                                       stem.find_first_not_of("0123456789") == std::string::npos;
                    if (named && IsCookedModule(ReadOpenPBRFile(PathUtf8(entry.path()))))
                        hashes.push_back(stem);
                }

                std::sort(hashes.begin(), hashes.end());
                std::string text;
                for (const std::string &hash : hashes)
                    text += hash + "\n";

                std::filesystem::create_directories(PathFromUtf8(CookedModuleDir()), ec);
                std::ofstream(PathFromUtf8(ShapeListPath()), std::ios::binary) << text;
            }

            for (const std::string &hash : hashes)
            {
                if (shapes.index.emplace(hash, static_cast<uint32_t>(shapes.hashes.size())).second)
                    shapes.hashes.push_back(hash);
            }
        }

        // A shape new to the session joins the end, and the project's list.
        uint32_t ShapeIndex(ShapeList &shapes, const std::string &hash)
        {
            SeedShapes(shapes);
            const auto [it, added] = shapes.index.emplace(hash, static_cast<uint32_t>(shapes.hashes.size()));
            if (added)
            {
                shapes.hashes.push_back(hash);
                std::error_code ec;
                std::filesystem::create_directories(PathFromUtf8(CookedModuleDir()), ec);
                std::ofstream(PathFromUtf8(ShapeListPath()), std::ios::binary | std::ios::app) << hash << "\n";
            }

            return it->second;
        }

        // The file holding a list entry's generated code: a registered module's, else the project's cooked module.
        // Empty for a rejected module and for a missing file.
        std::string ShapeCodePath(const std::string &hash)
        {
            for (const auto &module : Registry().modules)
            {
                if (module->hash == hash)
                    return module->rejected ? std::string() : module->code;
            }

            const std::string cooked = CookedModuleDir() + hash + ".slang";
            return AssetFileExists(cooked) ? cooked : std::string();
        }

        // Empty unless the file holds a module of this ABI with that hash.
        std::string ShapeCode(const std::string &hash, const std::string &path)
        {
            try
            {
                const std::string code = ReadOpenPBRFile(path);
                return IsCookedModule(code) && std::to_string(Fnv1a64(code)) == hash ? code : std::string();
            }
            catch (const std::exception &)
            {
                return {};
            }
        }

        OpenPBRPassSources WriteGroup(const std::string &text, uint32_t members)
        {
            const std::string dir = Path::RootPath() + "ShaderCache/MaterialX/";
            OpenPBRPassSources sources;
            sources.key = std::to_string(Fnv1a64(text));
            sources.members = members;

            const std::string stem = dir + "group_" + sources.key;
            sources.thin = stem + ".thin.slang";
            sources.shade = stem + ".shade.slang";
            sources.mask = stem + ".mask.slang";
            sources.forward = stem + ".forward.slang";
            sources.hit = stem + ".hit.slang";
            // The hit wrapper is written last: a group written before RT used groups gains it here.
            if (std::filesystem::exists(PathFromUtf8(sources.hit)))
                return sources;

            std::filesystem::create_directories(PathFromUtf8(dir));
            WriteOpenPBRFile(stem + ".slang", text);
            WriteWrapper(sources.thin, stem + ".slang", "MaterialThin");
            WriteWrapper(sources.shade, stem + ".slang", "MaterialShade");
            WriteWrapper(sources.mask, stem + ".slang", "MaterialMask");
            WriteWrapper(sources.forward, stem + ".slang", "MaterialForward");
            WriteWrapper(sources.hit, stem + ".slang", "MaterialHit");
            return sources;
        }

        // The group text, or a module that fails with the reason when the code cannot be combined.
        std::string CombineGroup(const std::vector<OpenPBRShapeCode> &members, uint32_t slots)
        {
            try
            {
                return CombineOpenPBRShapes(ReadOpenPBRFile(Registry().modules.front()->code), members, slots);
            }
            catch (const std::exception &e)
            {
                return std::string("#error \"") + e.what() + "\"\n";
            }
        }

        ResourceHandle<Image> LoadOpenPBRImage(CommandBuffer *cmd, const std::string &path)
        {
            ResourceHandle<Image> handle = ResourceManager::Get().Find<Image>(path);
            const auto preferred = PathFromUtf8(path).make_preferred().u8string();
            const std::string nativePath(reinterpret_cast<const char *>(preferred.data()), preferred.size());
            if (!handle)
                handle = ResourceManager::Get().Find<Image>(nativePath);

            if (handle)
                return handle;

            FileSystem file(path, std::ios::in | std::ios::binary);
            if (!file.IsOpen())
                return {};

            const auto data = file.ReadAllBytes();
            const bool floating = stbi_is_hdr_from_memory(data.data(), static_cast<int>(data.size())) != 0;
            Image *image = floating ? Image::LoadRGBA32F(cmd, path) : Image::LoadRGBA8(cmd, path);
            if (!image)
                return {};

            std::shared_ptr<Image> shared(image, [](Image *resident)
                                          { RHII.AddToDeletionQueue([resident]
                                                                    {
                    Image *retired = resident;
                    Image::Destroy(retired); }); });
            ResourceManager::Get().Register<Image>(path, shared);
            ResourceManager::Get().Register<Image>(nativePath, shared);
            return ResourceHandle<Image>(shared);
        }

        // MaterialX's renderer samples an image it cannot load as a 1x1 8-bit texture of the image node's "default"
        // (ImageHandler::acquireImage with mapValueToColor: a float fills red, vectors and colours fill rgb, alpha 1).
        ResourceHandle<Image> LoadOpenPBRDefaultImage(CommandBuffer *cmd, const OpenPBRMaterialSource &source, const std::string &identity)
        {
            vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
            const auto it = source.defaults.find(identity.substr(0, identity.rfind('.')) + ".default");
            if (it != source.defaults.end())
            {
                if (const float *value = std::get_if<float>(&it->second))
                    color.x = *value;
                else if (const vec2 *value2 = std::get_if<vec2>(&it->second))
                    color = vec4(*value2, 0.0f, 1.0f);
                else if (const vec3 *value3 = std::get_if<vec3>(&it->second))
                    color = vec4(*value3, 1.0f);
                else if (const vec4 *value4 = std::get_if<vec4>(&it->second))
                    color = *value4;
            }

            std::array<uint8_t, 4> texel{};
            for (int c = 0; c < 4; ++c)
                texel[c] = static_cast<uint8_t>(std::round(std::clamp(color[c], 0.0f, 1.0f) * 255.0f));

            Image *image = Image::LoadRawFromMemory(cmd, texel.data(), 1, 1, PE_FORMAT_R8G8B8A8_UNORM, "OpenPBR_default_" + identity);
            std::shared_ptr<Image> shared(image, [](Image *resident)
                                          { RHII.AddToDeletionQueue([resident]
                                                                    {
                    Image *retired = resident;
                    Image::Destroy(retired); }); });
            return ResourceHandle<Image>(shared);
        }

        void LoadSourceImages(OpenPBRMaterialSource &source)
        {
            if (source.textures.empty())
                return;

            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();

            for (auto &[identity, texture] : source.textures)
            {
                if (AssetFileExists(texture.file))
                    texture.image = LoadOpenPBRImage(cmd, texture.file);

                if (texture.image)
                    continue;

                PE_WARN("[OpenPBR] %s: cannot load image '%s' (%s); it samples as its default value, as in MaterialX",
                        source.path.c_str(),
                        texture.file.c_str(),
                        identity.c_str());
                texture.image = LoadOpenPBRDefaultImage(cmd, source, identity);
            }

            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }

        std::string CookSourceFiles(const std::string &json, const std::string &code, const std::string &cooked)
        {
            auto doc = ReadLayoutDocument(json, code);
            for (auto &item : doc["inputs"].GetArray())
            {
                if (item["type"] != "SamplerTexture2D")
                    continue;

                const auto file = PathFromUtf8(item["file"].GetString());
                const auto relative = std::filesystem::relative(file, PathFromUtf8(Path::AssetsPath()));
                if (!relative.empty() && *relative.begin() != "..")
                {
                    std::string portable = PathUtf8(relative);
                    std::replace(portable.begin(), portable.end(), '\\', '/');
                    item["file"].SetString(portable.c_str(), doc.GetAllocator());
                }
            }

            const std::string hash = std::to_string(Fnv1a64(code));
            doc.AddMember("module", rapidjson::Value(hash.c_str(), doc.GetAllocator()), doc.GetAllocator());
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            doc.Accept(writer);

            const auto dir = PathFromUtf8(Path::AssetsPath() + "MaterialX/Cooked/");
            std::filesystem::create_directories(PathFromUtf8(cooked).parent_path());
            const std::string module = PathUtf8(dir / (hash + ".slang"));
            WriteOpenPBRFile(module, code);
            WriteOpenPBRFile(cooked, buffer.GetString());
            return module;
        }

        std::shared_ptr<const OpenPBRMaterialSource> LoadCookedSource(
            const std::string &path, const std::string &material, const std::string &cooked)
        {
            const std::string json = ReadOpenPBRFile(cooked);
            rapidjson::Document doc;
            doc.Parse(json.c_str());
            if (doc.HasParseError() || !HasString(doc, "module"))
                throw std::runtime_error("cooked material has no module hash: " + cooked);

            const std::string hash = doc["module"].GetString();
            if (hash.empty() || hash.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("invalid cooked module hash: " + cooked);

            const std::string module = Path::AssetsPath() + "MaterialX/Cooked/" + hash + ".slang";
            const std::string code = ReadOpenPBRFile(module);
            if (std::to_string(Fnv1a64(code)) != hash)
                throw std::runtime_error("cooked module code hash mismatch: " + module);

            auto source = std::make_shared<OpenPBRMaterialSource>();
            source->path = path;
            source->material = material;
            source->layout = ReadLayout(json, code, *source);
            LoadSourceImages(*source);
            source->module = RegisterModule(path, module, code, source->layout);
            return source;
        }

        std::shared_ptr<const OpenPBRMaterialSource> GenerateSource(
            const std::string &path, const std::string &material, const std::string &cooked)
        {
            std::filesystem::path cook = PathFromUtf8(Path::ExecutablePath()) / "PhasmaCook";
#if defined(PE_WIN32)
            cook += ".exe";
#endif
            if (!std::filesystem::is_regular_file(cook))
            {
                throw std::runtime_error("PhasmaCook is unavailable next to the executable; regenerate materials with "
                                         "the matching editor and re-export cooked content");
            }

            const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto temp = PathFromUtf8(Path::RootPath() + "ShaderCache/MaterialX/_generate_" + std::to_string(serial));
            std::filesystem::create_directories(temp);
            std::shared_ptr<OpenPBRMaterialSource> source;
            try
            {
                const ProcessResult process = RunProcess(cook, {"--mtlx", path, PathUtf8(temp)},
                                                         {.timeoutMs = 60000, .captureOutput = true});
                if (!process.started || process.timedOut || process.exitCode != 0)
                    throw std::runtime_error("PhasmaCook failed or timed out: " + process.output);

                const auto stem = temp / PathFromUtf8(material);
                const std::string code = ReadOpenPBRFile(PathUtf8(stem) + ".slang");
                source = std::make_shared<OpenPBRMaterialSource>();
                source->path = path;
                source->material = material;
                const std::string json = ReadOpenPBRFile(PathUtf8(stem) + ".json");
                source->layout = ReadLayout(json, code, *source);
                LoadSourceImages(*source);
                const std::string module = CookSourceFiles(json, code, cooked);
                source->module = RegisterModule(path, module, code, source->layout);
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove_all(temp, ignored);
                throw;
            }

            std::error_code ignored;
            std::filesystem::remove_all(temp, ignored);
            return source;
        }

        std::string CookedSourcePath(const std::string &path, const std::string &material, const std::string &contentHash)
        {
            const auto parent = PathFromUtf8(path).parent_path();
            const auto assets = std::filesystem::absolute(PathFromUtf8(Path::AssetsPath())).lexically_normal();
            const auto relative = parent.lexically_relative(assets);
            const bool underAssets = !relative.empty() && *relative.begin() != "..";
            std::string context = PathUtf8(underAssets ? relative : parent);
            std::replace(context.begin(), context.end(), '\\', '/');
            context += "\nmodule_abi=" + std::to_string(kOpenPBRModuleAbi);

            // Identical documents in different directories can resolve relative image filenames differently.
            const std::string dir = Path::AssetsPath() + "MaterialX/Cooked/" + std::to_string(Fnv1a64(context)) + "/";
            return dir + contentHash + "." + material + ".json";
        }

        template <typename T>
        std::optional<T> ConstantInput(const MaterialInstance &instance, const char *name)
        {
            const OpenPBRInput *input = GetOpenPBRSource(instance).layout.Find(name);
            if (!input)
                return std::nullopt;

            const MaterialParamValue value = ResolveOpenPBRInput(instance, *input);
            if (const T *constant = std::get_if<T>(&value))
                return *constant;

            return std::nullopt;
        }
    } // namespace

    const OpenPBRInput *OpenPBRLayout::Find(const std::string &name) const
    {
        for (const OpenPBRInput &input : inputs)
        {
            if (input.identity == name || (input.node == surfaceNode && input.input == name))
                return &input;
        }

        return nullptr;
    }

    std::shared_ptr<const OpenPBRMaterialSource> GetOpenPBRDefaultSource()
    {
        return Registry().defaultSource;
    }

    const OpenPBRMaterialSource &GetOpenPBRSource(const MaterialInstance &instance)
    {
        const auto &source = instance.GetOpenPBRMaterialSource();
        return source ? *source : *GetOpenPBRDefaultSource();
    }

    const OpenPBRModule &GetOpenPBRModule(uint32_t index)
    {
        return *Registry().modules.at(index);
    }

    OpenPBRPassSources GetOpenPBRPassSources(const OpenPBRModule &module)
    {
        ShapeList &shapes = Shapes();
        const uint32_t index = ShapeIndex(shapes, module.hash);
        module.shapeIndex = index;

        const uint64_t size = ShapeGroupSize();
        const uint64_t first = index / size * size;
        const uint64_t end = std::min<uint64_t>(first + size, shapes.hashes.size());
        const uint32_t slots = static_cast<uint32_t>(end - first);

        // Keyed by the members' files, so a cache hit reads no code.
        std::string key = std::to_string(shapes.broken.size());
        std::vector<std::pair<uint64_t, std::string>> paths;
        for (uint64_t i = first; i < end; ++i)
        {
            std::string path = ShapeCodePath(shapes.hashes[i]);
            if (path.empty())
                continue;

            key += "\n" + path;
            paths.emplace_back(i, std::move(path));
        }

        if (const auto cached = shapes.sources.find(key); cached != shapes.sources.end())
            return cached->second;

        std::vector<OpenPBRShapeCode> members;
        for (const auto &[i, path] : paths)
        {
            std::string code = ShapeCode(shapes.hashes[i], path);
            if (!code.empty())
                members.push_back({static_cast<uint32_t>(i - first), shapes.hashes[i], std::move(code)});
        }

        std::string text = CombineGroup(members, slots);
        const bool broken = text.starts_with("#error") || shapes.broken.contains(std::to_string(Fnv1a64(text)));
        if (!broken || members.size() <= 1)
            return shapes.sources[key] = WriteGroup(text, static_cast<uint32_t>(members.size()));

        // A group that failed to compile builds each member alone, so a broken shape only fails its own modules.
        // ponytail: not cached (it depends on the module); a broken group re-reads its members per build.
        std::erase_if(members, [&](const OpenPBRShapeCode &member)
                      { return member.hash != module.hash; });
        return WriteGroup(CombineGroup(members, slots), static_cast<uint32_t>(members.size()));
    }

    uint32_t GetOpenPBRShapeSlot(const OpenPBRModule &module)
    {
        const uint32_t index = module.shapeIndex;
        return index == UINT32_MAX ? UINT32_MAX : index % ShapeGroupSize();
    }

    void MarkOpenPBRGroupBroken(const std::string &key)
    {
        Shapes().broken.insert(key);
    }

    OpenPBRHitSet MakeOpenPBRHitSet(const std::vector<uint32_t> &modules)
    {
        std::vector<OpenPBRPassSources> sources;
        for (uint32_t module : modules)
            sources.push_back(GetOpenPBRPassSources(GetOpenPBRModule(module)));

        // Groups in shape-list order, not the scene's: the same modules in another order keep their hit groups
        // (RayTracingPass::RetargetHitSet), so reordered nodes rebuild nothing.
        std::vector<size_t> order(modules.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                         { return GetOpenPBRModule(modules[a]).shapeIndex < GetOpenPBRModule(modules[b]).shapeIndex; });

        OpenPBRHitSet set;
        set.modules = modules;
        set.moduleGroups.resize(modules.size());
        for (size_t i : order)
        {
            const auto group = std::find_if(set.groups.begin(), set.groups.end(), [&](const OpenPBRHitGroup &g)
                                            { return g.key == sources[i].key; });
            set.moduleGroups[i] = static_cast<uint32_t>(group - set.groups.begin());
            if (group == set.groups.end())
                set.groups.push_back({sources[i].key, sources[i].hit, sources[i].members});
        }

        return set;
    }

    std::shared_ptr<const OpenPBRMaterialSource> LoadOpenPBRMaterial(
        const std::string &path, const std::string &material, std::string &error)
    {
        const std::filesystem::path requested = PathFromUtf8(path);
        const std::filesystem::path resolved = requested.is_absolute() ? requested : PathFromUtf8(Path::ResolveAsset(path));
        const std::string absolute = AbsoluteShaderPath(resolved);
        FileSystem document(absolute, std::ios::in | std::ios::ate);
        const std::string text = document.IsOpen() ? document.ReadAll() : std::string();
        const std::string contentHash = std::to_string(Fnv1a64(text));
        const std::string key = absolute + "\n" + material + "\n" + contentHash;
        auto [it, inserted] = Registry().sources.try_emplace(key);
        SourceResult &result = it->second;
        if (inserted)
        {
            try
            {
                if (text.empty())
                    throw std::runtime_error("document is missing, empty or unreadable");

                const bool pathName = material.find_first_of("/\\:") != std::string::npos ||
                                      material == "." || material == "..";
                if (material.empty() || pathName)
                {
                    throw std::runtime_error("invalid MaterialX material name");
                }

                const std::string cooked = CookedSourcePath(absolute, material, contentHash);
                if (AssetFileExists(cooked))
                    result.source = LoadCookedSource(absolute, material, cooked);
                else
                    result.source = GenerateSource(absolute, material, cooked);
            }
            catch (const std::exception &failure)
            {
                result.error = "[OpenPBR] " + absolute + ": " + failure.what();
                std::replace(result.error.begin(), result.error.end(), '\r', ' ');
                std::replace(result.error.begin(), result.error.end(), '\n', ' ');
                PE_WARN("%s", result.error.c_str());
            }
        }

        error = result.error;
        return result.source;
    }

    bool OpenPBREligible(Material &material, const OpenPBRModule &module)
    {
        const PassInfoAsset *pass = material.passInfoAsset.get();
        const bool standard = !pass || pass->GetFilePath().filename() == "standard_pbr.pass";

        return standard && module.layout.valid;
    }

    static std::atomic<uint32_t> s_openpbrRejections = 0;

    uint32_t OpenPBRRejectionCount()
    {
        return s_openpbrRejections.load();
    }

    void RejectOpenPBRModule(const OpenPBRModule &module, const std::string &error)
    {
        if (module.rejected.exchange(true))
            return;

        s_openpbrRejections.fetch_add(1);

        // Compiler output follows a summary ("slangc exit 1:") and may open with warnings that quote source lines, so
        // take a diagnostic: slangc's "error[E30015]: ..." or DXC's "file:line:col: error: ...". Without one, the
        // failure's own first line.
        std::string line = error.substr(0, error.find('\n'));
        std::istringstream lines(error);
        for (std::string next; std::getline(lines, next);)
        {
            if (next.starts_with("error") || next.find(": error") != std::string::npos)
            {
                line = next;
                break;
            }
        }

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        PE_WARN("[OpenPBR] %s: shader compile failed: %s", module.document.c_str(), line.c_str());
    }

    static bool MeshHasEligibleOpenPBR(const Mesh &mesh)
    {
        if (!mesh.material || !mesh.materialInstance || !mesh.materialInstance->GetOpenPBR())
            return false;

        const OpenPBRModule &module = *GetOpenPBRSource(*mesh.materialInstance).module;
        return !module.rejected && OpenPBREligible(*mesh.material, module);
    }

    bool MeshUsesOpenPBR(const Mesh &mesh)
    {
        const RenderType type = mesh.renderType;
        const bool opaque = type == RenderType::Opaque || type == RenderType::AlphaCut;
        return opaque && MeshHasEligibleOpenPBR(mesh);
    }

    bool MeshUsesOpenPBRForward(const Mesh &mesh)
    {
        const RenderType type = mesh.renderType;
        const bool blended = type == RenderType::AlphaBlend || type == RenderType::Transmission;
        return blended && MeshHasEligibleOpenPBR(mesh);
    }

    static bool ValidateOpenPBRAssignment(Scene &scene, const Mesh &mesh,
                                          const OpenPBRMaterialSource &source, std::string &error)
    {
        if (!mesh.material || !OpenPBREligible(*mesh.material, *source.module))
        {
            error = "[OpenPBR] Cannot assign to a custom shader material or use an unavailable module (see the log)";
            return false;
        }

        if (!scene.CanUseOpenPBRImages(source, error))
        {
            PE_WARN("%s", error.c_str());
            return false;
        }

        return true;
    }

    bool SelectOpenPBRRenderType(Mesh &mesh)
    {
        if (!mesh.materialInstance || mesh.renderType != RenderType::Opaque)
            return false;

        // A connected weight has no constant (and no layout input): see the header for why it keeps the render type.
        const auto weight = ConstantInput<float>(*mesh.materialInstance, "transmission_weight");
        if (!weight || *weight <= 0.0f)
            return false;

        mesh.renderType = RenderType::Transmission;
        mesh.materialInstance->SetRenderType(RenderType::Transmission);
        return true;
    }

    bool AssignOpenPBRMaterial(Scene &scene, Mesh &mesh,
                               std::shared_ptr<const OpenPBRMaterialSource> source, std::string &error)
    {
        if (!ValidateOpenPBRAssignment(scene, mesh, *source, error))
            return false;

        MaterialInstance &instance = *scene.CreateMaterialInstance(mesh);
        instance.SetOpenPBRMaterialSource(std::move(source));
        MirrorOpenPBRToLegacy(instance);
        if (SelectOpenPBRRenderType(mesh))
            scene.SetInstancesDirty(); // the mesh leaves the visibility path for the forward pass
        scene.SetTexturesDirty();      // layout and editorFlags need a full material-table + mesh-constant rebuild
        scene.SetMaterialDirty();
        return true;
    }

    bool SetOpenPBREnabled(Scene &scene, Mesh &mesh, bool enabled, std::string &error)
    {
        if (!mesh.material)
        {
            error = "[OpenPBR] This mesh has no material";
            return false;
        }

        const auto source = mesh.materialInstance
                                ? mesh.materialInstance->GetOpenPBRMaterialSource()
                                : mesh.material->openpbrSource;
        const auto activeSource = source ? source : GetOpenPBRDefaultSource();
        if (enabled && !ValidateOpenPBRAssignment(scene, mesh, *activeSource, error))
            return false;

        if (!enabled && !mesh.materialInstance)
            return true;

        MaterialInstance &instance = *scene.CreateMaterialInstance(mesh);
        if (instance.GetOpenPBR() == enabled)
            return true;

        instance.SetOpenPBR(enabled);
        if (enabled)
            MirrorOpenPBRToLegacy(instance);

        scene.SetTexturesDirty(); // enabled state changes the byte layout and editorFlags
        scene.SetMaterialDirty();
        return true;
    }

    bool SetOpenPBRInput(Scene &scene, MaterialInstance &instance, const std::string &name,
                         const MaterialParamValue &value, std::string &error)
    {
        const OpenPBRMaterialSource &source = GetOpenPBRSource(instance);
        const OpenPBRInput *input = source.layout.Find(name);
        if (!source.module->layout.valid || !input)
        {
            error = "[OpenPBR] Unknown input or unavailable layout: " + name;
            return false;
        }

        if (input->type == "SamplerTexture2D")
        {
            error = "[OpenPBR] Texture filenames are read-only: " + name;
            return false;
        }

        const bool matchingType = value.index() == source.defaults.at(input->identity).index();
        const bool validBool = input->type != "bool" ||
                               (std::holds_alternative<uint32_t>(value) && std::get<uint32_t>(value) <= 1u);
        if (!matchingType || !validBool)
        {
            error = "[OpenPBR] " + name + " expects " + input->type;
            return false;
        }

        instance.SetParam(input->identity, value);
        MirrorOpenPBRToLegacy(instance);
        scene.SetMaterialDirty();
        return true;
    }

    MaterialParamValue ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input)
    {
        const MaterialParamValue &fallback = GetOpenPBRSource(instance).defaults.at(input.identity);
        if (instance.HasParamOverride(input.identity))
        {
            const MaterialParamValue value = instance.GetParam(input.identity);
            if (value.index() == fallback.index())
                return value;
        }

        const auto it = instance.GetParent()->params.find(input.identity);
        if (it != instance.GetParent()->params.end() && it->second.index() == fallback.index())
            return it->second;

        return fallback;
    }

    static uint32_t OpenPBRSamplerIndex(const MaterialInstance &instance, const OpenPBRInput &texture)
    {
        const OpenPBRLayout &layout = GetOpenPBRSource(instance).layout;
        auto integer = [&](const char *name, int32_t fallback)
        {
            const OpenPBRInput *input = layout.Find(texture.node + "." + name);
            return input ? std::get<int32_t>(ResolveOpenPBRInput(instance, *input)) : fallback;
        };

        // ponytail: constant addressing uses clamp; implement border/default sampling when a graph needs it.
        const uint32_t u = static_cast<uint32_t>(std::clamp(integer("uaddressmode", 2), 1, 3) - 1);
        const uint32_t v = static_cast<uint32_t>(std::clamp(integer("vaddressmode", 2), 1, 3) - 1);
        // ponytail: cubic filtering uses linear; add cubic reconstruction when a graph needs it.
        const uint32_t filter = integer("filtertype", 1) == 0 ? 0u : 1u;
        return (u * 3u + v) * 2u + filter;
    }

    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance,
                                           const std::unordered_map<ImageView *, uint32_t> &imageViewIndices)
    {
        const OpenPBRMaterialSource &source = GetOpenPBRSource(instance);
        const OpenPBRModule &module = *source.module;
        if (!module.layout.valid)
            return {};

        std::vector<uint8_t> bytes(kOpenPBRInputsOffset + module.layout.byteSize, 0);
        const MaterialGpuData legacy = instance.BuildGpuData();
        std::memcpy(bytes.data(), &legacy, sizeof(legacy));
        std::memcpy(bytes.data() + kOpenPBRModuleOffset, &module.index, sizeof(module.index));

        for (const OpenPBRInput &input : source.layout.inputs)
        {
            const uint32_t offset = kOpenPBRInputsOffset + input.offset;
            if (input.type == "SamplerTexture2D")
            {
                ImageView *view = source.textures.at(input.identity).image->GetSRV();
                const auto it = imageViewIndices.find(view);
                if (it == imageViewIndices.end())
                    return {};

                WriteParamValue(bytes, offset, it->second);
                WriteParamValue(bytes, offset + sizeof(uint32_t), OpenPBRSamplerIndex(instance, input));
            }
            else
            {
                WriteParamValue(bytes, offset, ResolveOpenPBRInput(instance, input));
            }
        }

        return bytes;
    }

    void MirrorOpenPBRToLegacy(MaterialInstance &instance)
    {
        if (!GetOpenPBRSource(instance).module->layout.valid)
            return;

        const auto base = ConstantInput<vec3>(instance, "base_color");
        const auto weight = ConstantInput<float>(instance, "base_weight");
        if (base && weight)
        {
            // Legacy colours are sRGB-authored; OpenPBR's are linear.
            const vec3 color = glm::clamp(*base * *weight, vec3(0.0f), vec3(1.0f));
            instance.SetBaseColorFactor(vec4(glm::convertLinearToSRGB(color), instance.GetBaseColorFactor().a));
        }

        // RT hits, shadow rays and the off path take a transparent mesh's opacity from the base colour's alpha.
        if (const auto opacity = ConstantInput<float>(instance, "geometry_opacity"))
        {
            vec4 color = instance.GetBaseColorFactor();
            color.a = std::clamp(*opacity, 0.0f, 1.0f);
            instance.SetBaseColorFactor(color);
        }

        // The legacy hit's glass (RT refraction): the weight, the entry tint in the one base colour both lobes share
        // (blended by the weight, from the constant base or white so repeated mirrors agree) and Beer's-law volume.
        const auto transmission = ConstantInput<float>(instance, "transmission_weight");
        if (transmission && *transmission > 0.0f)
        {
            const float glass = std::clamp(*transmission, 0.0f, 1.0f);
            const vec3 color = glm::clamp(ConstantInput<vec3>(instance, "transmission_color").value_or(vec3(1.0f)), 0.0f, 1.0f);
            const float depth = ConstantInput<float>(instance, "transmission_depth").value_or(0.0f);
            const float coatWeight = std::clamp(ConstantInput<float>(instance, "coat_weight").value_or(0.0f), 0.0f, 1.0f);
            const vec3 coatColor = glm::clamp(ConstantInput<vec3>(instance, "coat_color").value_or(vec3(1.0f)), 0.0f, 1.0f);
            const vec3 coat = glm::sqrt(glm::mix(vec3(1.0f), coatColor, coatWeight));
            const vec3 tint = depth > 0.0f ? coat : color * coat;

            const vec3 opaque = base && weight ? glm::clamp(*base * *weight, vec3(0.0f), vec3(1.0f)) : vec3(1.0f);
            const vec3 mixed = glm::mix(opaque, tint, glass);
            instance.SetBaseColorFactor(vec4(glm::convertLinearToSRGB(mixed), instance.GetBaseColorFactor().a));
            instance.SetTransmissionFactor(glass);
            if (depth > 0.0f)
            {
                instance.SetAttenuationColor(color);
                instance.SetAttenuationDistance(depth);
            }
        }

        if (const auto value = ConstantInput<float>(instance, "base_metalness"))
            instance.SetMetallic(*value);

        if (const auto value = ConstantInput<float>(instance, "specular_roughness"))
            instance.SetRoughness(*value);

        if (const auto value = ConstantInput<float>(instance, "specular_ior"))
            instance.SetIor(*value);

        const auto emission = ConstantInput<vec3>(instance, "emission_color");
        const auto luminance = ConstantInput<float>(instance, "emission_luminance");
        if (emission && luminance)
        {
            // Invert the legacy HDR sRGB encoding, whose peak above 1 carries brightness.
            const vec3 color = glm::max(*emission * *luminance, vec3(0.0f));
            const float peak = std::max({color.r, color.g, color.b, 1.0f});
            instance.SetEmissiveFactor(glm::convertLinearToSRGB(color / peak) * peak);
        }
    }

    vec3 OpenPBRShadowTransmittance(const MaterialInstance &instance)
    {
        // Connected inputs take OpenPBR's defaults.
        const float opacity = std::clamp(ConstantInput<float>(instance, "geometry_opacity").value_or(1.0f), 0.0f, 1.0f);
        const float metalness = ConstantInput<float>(instance, "base_metalness").value_or(0.0f);
        const float weight = ConstantInput<float>(instance, "transmission_weight").value_or(0.0f);
        const float transmitted = std::clamp((1.0f - metalness) * weight, 0.0f, 1.0f);

        // With a volume, transmission_color is reached after transmission_depth: two surfaces of an object about that
        // thick give it, so one surface keeps its square root.
        vec3 tint = glm::clamp(ConstantInput<vec3>(instance, "transmission_color").value_or(vec3(1.0f)), 0.0f, 1.0f);
        if (ConstantInput<float>(instance, "transmission_depth").value_or(0.0f) > 0.0f)
            tint = glm::sqrt(tint);

        // coat_color tints light that crosses the coat twice (in and out), so one crossing keeps its square root.
        const float coatWeight = std::clamp(ConstantInput<float>(instance, "coat_weight").value_or(0.0f), 0.0f, 1.0f);
        const vec3 coatColor = glm::clamp(ConstantInput<vec3>(instance, "coat_color").value_or(vec3(1.0f)), 0.0f, 1.0f);
        const vec3 coat = glm::sqrt(glm::mix(vec3(1.0f), coatColor, coatWeight));

        // ponytail: Fresnel reflection, refraction's focusing (caustics) and graph-driven inputs are ignored; a
        // generated per-texel transmittance (like pe_material_mask) is the upgrade path.
        return vec3(1.0f - opacity) + opacity * transmitted * tint * coat;
    }
} // namespace pe
