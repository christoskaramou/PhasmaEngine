#include "MaterialXGen.h"
#include "PeSurfaceNodeSlang.h"
#include "Scene/OpenPBRMaterial.h"
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXGenHw/HwConstants.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/ShaderGraph.h>
#include <MaterialXGenShader/UnitSystem.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXGenSlang/SlangShaderGenerator.h>
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pe
{
    namespace mx = MaterialX;

    namespace
    {
        // One material input as PE stores it: a static in the module, loaded from the material's byte range.
        struct PeMaterialInput
        {
            std::string var;   // generated static name, e.g. open_pbr_surface_surfaceshader_base_color
            std::string input; // input name on its node, e.g. base_color
            std::string node;  // owning node path
            std::string type;  // Slang type name from the generator's syntax
            uint32_t offset = 0;
            std::string value; // MaterialX value string, empty when the port has none
            std::string file;
            bool alias = false;
        };

        uint32_t PeTypeSize(const std::string &type)
        {
            if (type == "float" || type == "int" || type == "uint" || type == "bool")
                return 4;
            if (type == "float2")
                return 8;
            if (type == "float3")
                return 12;
            if (type == "float4")
                return 16;
            if (type == "float3x3")
                return 36;
            if (type == "float4x4")
                return 64;
            if (type == "SamplerTexture2D")
                return 8;

            return 0;
        }

        // The exact lines of pe_load_material for one input (format pinned by .testpool/tests/mtlx_codegen.py).
        std::vector<std::string> LoaderLines(const PeMaterialInput &in)
        {
            const std::string &n = in.var;
            const std::string &t = in.type;
            auto at = [&](uint32_t extra)
            { return "o + " + std::to_string(in.offset + extra); };

            if (t == "float")
                return {n + " = asfloat(b.Load(" + at(0) + "));"};
            if (t == "float2" || t == "float3" || t == "float4")
                return {n + " = asfloat(b.Load" + t.substr(5) + "(" + at(0) + "));"};
            if (t == "int")
                return {n + " = asint(b.Load(" + at(0) + "));"};
            if (t == "uint")
                return {n + " = b.Load(" + at(0) + ");"};
            if (t == "bool")
                return {n + " = b.Load(" + at(0) + ") != 0u;"};
            if (t == "SamplerTexture2D")
                return {n + ".textureIndex = b.Load(" + at(0) + ");", n + ".samplerIndex = b.Load(" + at(4) + ");"};

            std::vector<std::string> rows;
            const bool m3 = t == "float3x3";
            for (uint32_t r = 0; r < (m3 ? 3u : 4u); ++r)
            {
                rows.push_back(n + "[" + std::to_string(r) + "] = asfloat(b.Load" + (m3 ? "3" : "4") + "(" +
                               at(r * (m3 ? 12u : 16u)) + "));");
            }

            return rows;
        }

        bool References(const std::string &text, const std::string &name)
        {
            return std::regex_search(text, std::regex("\\b" + name + "\\b"));
        }

        // RT hit stages (PE_MATERIAL_HIT) have no derivatives: zero differentials there (mip 0, as legacy hits sample),
        // fine derivatives everywhere else, as MaterialX's GLSL reference gets them on our drivers (DXIL's plain ddx/fwidth
        // are coarse). Subsurface's curvature ratio would become 0/0 in hits, so they read the hit triangle's.
        constexpr const char *kDerivativeSeams = R"(#ifdef PE_MATERIAL_HIT
#define pe_ddx(x) ((x) * 0.0)
#define pe_ddy(x) ((x) * 0.0)
#define pe_fwidth(x) ((x) * 0.0)
#define pe_ddx_fine(x) ((x) * 0.0)
#define pe_ddy_fine(x) ((x) * 0.0)
#define pe_ddx_coarse(x) ((x) * 0.0)
#define pe_ddy_coarse(x) ((x) * 0.0)
float pe_curvature(float3 N, float3 P)
{
    return pe_hit_curvature;
}
#else
#define pe_ddx(x) ddx_fine(x)
#define pe_ddy(x) ddy_fine(x)
#define pe_fwidth(x) (abs(ddx_fine(x)) + abs(ddy_fine(x)))
#define pe_ddx_fine(x) ddx_fine(x)
#define pe_ddy_fine(x) ddy_fine(x)
#define pe_ddx_coarse(x) ddx_coarse(x)
#define pe_ddy_coarse(x) ddy_coarse(x)
#ifdef PE_SHADE_ONCE
// Material pass 2 shades once per pixel: its quad-mapped stage 1 computes this curvature before the BSDF stage.
static float pe_shade_curvature;
float pe_curvature(float3 N, float3 P)
{
    return pe_shade_curvature;
}
#else
float pe_curvature(float3 N, float3 P)
{
    return length(pe_fwidth(N)) / length(pe_fwidth(P));
}
#endif
#endif
)";

        bool AddDerivativeSeams(std::string &text, const std::string &name, std::string &error)
        {
            const std::string curvature = "length(fwidth(N)) / length(fwidth(P))";
            size_t at = text.find(curvature);
            if (at == std::string::npos && References(text, "mx_subsurface_scattering_approx"))
            {
                error = name + ": subsurface curvature expression not found (MaterialX changed it; update the hit seam)";
                return false;
            }

            while (at != std::string::npos)
            {
                text.replace(at, curvature.size(), "pe_curvature(N, P)");
                at = text.find(curvature, at);
            }

            static const std::regex derivative(R"((^|[^\w.])(ddx|ddy|fwidth|ddx_fine|ddy_fine|ddx_coarse|ddy_coarse)(?=\s*\())");
            text = kDerivativeSeams + std::regex_replace(text, derivative, "$1pe_$2");
            return true;
        }

        // [first, last] of "<header> ... { ... }" with balanced braces, or npos when absent.
        std::pair<size_t, size_t> FindBlock(const std::string &text, const std::string &header)
        {
            const size_t start = text.find(header);
            if (start == std::string::npos)
                return {std::string::npos, std::string::npos};

            size_t i = text.find('{', start);
            int depth = 0;
            for (; i < text.size(); ++i)
            {
                if (text[i] == '{')
                    ++depth;
                else if (text[i] == '}')
                    --depth;

                if (depth == 0 && text[i] == '}')
                    return {start, i};
            }

            return {std::string::npos, std::string::npos};
        }

        // "0.8, 0.8, 0.8" -> [0.8,0.8,0.8]; booleans -> [1]/[0]; textures -> [].
        std::string JsonDefault(const PeMaterialInput &in)
        {
            if (in.type == "int" && in.value.empty())
                return "[0]";

            if (in.type == "SamplerTexture2D" || in.value.empty())
                return "[]";
            if (in.type == "bool")
                return in.value == "true" ? "[1]" : "[0]";

            std::string out = "[";
            std::stringstream ss(in.value);
            std::string item;
            bool first = true;
            while (std::getline(ss, item, ','))
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.9g", std::stod(item));
                out += (first ? "" : ",") + std::string(buf);
                first = false;
            }

            return out + "]";
        }

        std::string SurfaceInputStruct(const mx::ShaderNode &surface, mx::GenContext &context)
        {
            std::string text = "struct PeSurfaceInputs\n{\n";
            for (const mx::ShaderInput *input : surface.getInputs())
            {
                const std::string type = context.getShaderGenerator().getSyntax().getTypeName(input->getType());
                if (type != "float" && type != "float3" && type != "bool")
                    throw std::runtime_error("unsupported evaluated OpenPBR input type '" + type + "'");

                text += "    " + type + " " + input->getName() + ";\n";
            }
            return text + "};\n\n";
        }

        std::string SurfaceDirectionHelpers()
        {
            return "float3 pe_blend_direction(float3 a, float3 b, float t)\n"
                   "{\n"
                   "    float3 v = lerp(a, b, t);\n"
                   "    if (dot(v, v) <= 1e-12)\n"
                   "        v = a;\n"
                   "    const float len2 = dot(v, v);\n"
                   "    return len2 <= 1e-12 ? float3(0.0, 0.0, 1.0) : v * rsqrt(len2);\n"
                   "}\n\n"
                   "float3 pe_surface_tangent(float3 v, float3 n)\n"
                   "{\n"
                   "    v -= n * dot(n, v);\n"
                   "    if (dot(v, v) <= 1e-12)\n"
                   "        v = abs(n.x) < 0.9 ? cross(n, float3(1.0, 0.0, 0.0)) : cross(n, float3(0.0, 1.0, 0.0));\n"
                   "    return normalize(v);\n"
                   "}\n\n";
        }

        std::string SurfaceInputBlend(const mx::ShaderNode &surface, mx::GenContext &context)
        {
            std::string text = SurfaceDirectionHelpers() +
                               "PeSurfaceInputs pe_blend_inputs(PeSurfaceInputs a, PeSurfaceInputs b, float t)\n{\n"
                               "    PeSurfaceInputs p;\n";
            for (const mx::ShaderInput *input : surface.getInputs())
            {
                const std::string &name = input->getName();
                const std::string type = context.getShaderGenerator().getSyntax().getTypeName(input->getType());
                const bool normal = name == "geometry_normal" || name == "geometry_coat_normal";
                const std::string first = "a." + name;
                const std::string second = "b." + name;
                const std::string value = type == "bool" ? "t < 0.5 ? " + first + " : " + second
                                                         : (normal ? "pe_blend_direction(" : "lerp(") +
                                                               first + ", " + second + ", t)";
                text += "    p." + name + " = " + value + ";\n";
            }

            text += "    p.geometry_tangent = pe_surface_tangent(p.geometry_tangent, p.geometry_normal);\n"
                    "    p.geometry_coat_tangent = pe_surface_tangent(p.geometry_coat_tangent, p.geometry_coat_normal);\n"
                    "    return p;\n}\n\n";
            return text;
        }

        std::string SurfaceInputShade(const mx::ShaderNode &surface)
        {
            std::string text = "#ifndef PE_MATERIAL_THIN\n"
                               "float4 pe_shade_inputs(PeSurfaceInputs p)\n{\n"
                               "    surfaceshader result;\n"
                               "    NG_open_pbr_surface_surfaceshader(\n";
            for (const mx::ShaderInput *input : surface.getInputs())
                text += "        p." + input->getName() + ",\n";

            return text + "        result);\n"
                          "    return float4(result.color, 1.0);\n}\n#endif\n\n";
        }

        void CaptureThinInputs(std::string &text, const mx::ShaderNode &surface, mx::GenContext &context)
        {
            const auto [start, end] = FindBlock(text, "float4 pe_material_fragment(");
            const size_t call = text.find("NG_open_pbr_surface_surfaceshader(", start);
            if (start == std::string::npos || call == std::string::npos || call > end)
                throw std::runtime_error("open_pbr_surface call not found in pe_material_fragment");

            // Keep MaterialX's topological upstream evaluation, including UV and colour transforms, before the closure.
            const size_t body = text.find('{', start);
            const size_t callLine = text.rfind('\n', call) + 1;
            std::string evaluator = "PeSurfaceInputs pe_evaluate_inputs(VertexData _vd)\n{" +
                                    text.substr(body + 1, callLine - body - 1) + "    PeSurfaceInputs p;\n";
            for (const mx::ShaderInput *input : surface.getInputs())
            {
                evaluator += "    p." + input->getName() + " = " +
                             context.getShaderGenerator().getUpstreamResult(input, context) + ";\n";
            }

            evaluator += "    return p;\n}\n\n";
            text.replace(start, end - start + 1,
                         SurfaceInputStruct(surface, context) + evaluator +
                             SurfaceInputBlend(surface, context) + SurfaceInputShade(surface));
        }

        std::string SurfaceFrameEvaluator(bool hasUv)
        {
            // Without texcoord_0 both sprite frames evaluate identical inputs: one evaluation is the blend.
            if (!hasUv)
            {
                return "PeSurfaceInputs pe_material_inputs(float2 nextUv, float blend)\n{\n"
                       "    return pe_evaluate_inputs(vd);\n}\n\n";
            }

            const std::string text = "PeSurfaceInputs pe_material_inputs(float2 nextUv, float blend)\n{\n"
                                     "    const VertexData current = vd;\n"
                                     "    VertexData next = current;\n"
                                     "    next.texcoord_0 = nextUv;\n";

            return text + "\n"
                          "    if (blend >= 1.0)\n"
                          "    {\n"
                          "        const PeSurfaceInputs p = pe_evaluate_inputs(next);\n"
                          "        vd = current;\n"
                          "        return p;\n"
                          "    }\n\n"
                          "    PeSurfaceInputs p = pe_evaluate_inputs(current);\n"
                          "    if (blend > 0.0)\n"
                          "    {\n"
                          "        const PeSurfaceInputs q = pe_evaluate_inputs(next);\n"
                          "        vd = current;\n"
                          "        p = pe_blend_inputs(p, q, blend);\n"
                          "    }\n"
                          "    return p;\n}\n\n";
        }

        // An image's filename as MaterialX resolves it (fileprefix included), made absolute next to its document.
        std::string JsonFile(const mx::DocumentPtr &doc,
                             const std::string &inputPath,
                             const std::filesystem::path &document)
        {
            const mx::ElementPtr element = doc->getDescendant(inputPath);
            const mx::ValueElementPtr input = element ? element->asA<mx::ValueElement>() : nullptr;
            if (!input)
                throw std::runtime_error("no filename input '" + inputPath + "'");

            // Narrow paths, as everywhere in this generator (MaterialX's own FilePath is narrow).
            const mx::FilePath resolved(input->getResolvedValueString());
            const std::filesystem::path path(resolved.asString(mx::FilePath::FormatPosix));
            const std::filesystem::path absolute = std::filesystem::absolute(document.parent_path() / path);
            const std::string filename = absolute.lexically_normal().generic_string();

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            writer.String(filename.c_str(), static_cast<rapidjson::SizeType>(filename.size()));
            return buffer.GetString();
        }
    } // namespace

    bool GenerateMaterialXSlang(const std::filesystem::path &mtlx, const std::filesystem::path &outDir, std::string &error)
    {
        try
        {
            const std::filesystem::path mxRoot = std::filesystem::path(Path::ExecutablePath()) / "MaterialX";
            mx::FileSearchPath libraryPath(mx::FilePath(mxRoot.string()));
            mx::DocumentPtr libs = mx::createDocument();
            mx::loadLibraries({"libraries"}, libraryPath, libs);

            mx::DocumentPtr doc = mx::createDocument();
            mx::FileSearchPath docPath(mx::FilePath(mtlx.parent_path().string()));
            mx::readFromXmlFile(doc, mx::FilePath(mtlx.string()), docPath);
            doc->setDataLibrary(libs);

            std::string message;
            if (!doc->validate(&message))
            {
                error = mtlx.string() + " is not a valid MaterialX document: " + message;
                return false;
            }

            mx::ShaderGeneratorPtr gen = mx::SlangShaderGenerator::create();
            gen->registerImplementation("IM_surface_" + mx::SlangShaderGenerator::TARGET, PeSurfaceNodeSlang::create);
            mx::GenContext ctx(gen);

            // Library includes are root-relative ("libraries/stdlib/..."), first match wins: PE's overrides
            // (pe_overrides/libraries/...) shadow MaterialX's files of the same relative path.
            ctx.registerSourceCodeSearchPath(mx::FilePath((mxRoot / "pe_overrides").string()));
            ctx.registerSourceCodeSearchPath(mx::FilePath(mxRoot.string()));
            ctx.registerSourceCodeSearchPath(mx::FilePath(mtlx.parent_path().string()));

            mx::GenOptions &options = ctx.getOptions();
            options.shaderInterfaceType = mx::SHADER_INTERFACE_COMPLETE;
            options.hwSpecularEnvironmentMethod = mx::SPECULAR_ENVIRONMENT_FIS;
            options.hwTransmissionRenderMethod = mx::TRANSMISSION_REFRACTION;
            options.hwMaxActiveLightSources = 0; // PeSurfaceNodeSlang emits PE's own loop
            options.targetColorSpaceOverride = "lin_rec709";
            options.targetDistanceUnit = "meter";
            options.fileTextureVerticalFlip = false;

            mx::DefaultColorManagementSystemPtr cms = mx::DefaultColorManagementSystem::create(gen->getTarget());
            cms->loadLibrary(libs);
            gen->setColorManagementSystem(cms);

            mx::UnitSystemPtr units = mx::UnitSystem::create(gen->getTarget());
            mx::UnitConverterRegistryPtr registry = mx::UnitConverterRegistry::create();
            for (const char *unitType : {"distance", "angle"})
            {
                mx::UnitTypeDefPtr def = libs->getUnitTypeDef(unitType);
                if (def)
                    registry->addUnitConverter(def, mx::LinearUnitConverter::create(def));
            }
            units->loadLibrary(libs);
            units->setUnitConverterRegistry(registry);
            gen->setUnitSystem(units);

            std::vector<mx::TypedElementPtr> elements = mx::findRenderableElements(doc);
            if (elements.empty())
            {
                error = mtlx.string() + " has no renderable material";
                return false;
            }

            std::filesystem::create_directories(outDir);

            for (const mx::TypedElementPtr &elem : elements)
            {
                const std::string name = elem->getName();
                mx::ShaderPtr shader = gen->generate(name, elem, ctx);
                if (!shader)
                {
                    error = name + ": MaterialX generated no shader";
                    return false;
                }

                std::string text = shader->getSourceCode(mx::Stage::PIXEL);
                const mx::ShaderStage &pixel = shader->getStage(mx::Stage::PIXEL);

                mx::NodePtr material = elem->asA<mx::Node>();
                mx::NodePtr surface = material ? material->getConnectedNode("surfaceshader") : nullptr;
                if (!surface || surface->getCategory() != "open_pbr_surface")
                    throw std::runtime_error(name + ": expected an open_pbr_surface material");

                const mx::ShaderGraph &graph = shader->getGraph();
                const mx::ShaderNode *surfaceNode = graph.getNode(surface->getNamePath());
                if (!surfaceNode)
                    throw std::runtime_error(name + ": open_pbr_surface not found in ShaderGraph");

                if (References(text, "discard"))
                    throw std::runtime_error(name + ": discard cannot be dropped by pass 1 (opacity requires Phase 4)");

                // --- Material inputs: the public uniforms, in generator order, tightly packed. ---
                std::vector<PeMaterialInput> inputs;
                std::vector<std::string> dropped; // aggregate members (surfaceshader, displacementshader)
                std::map<std::pair<std::string, std::string>, size_t> identities;
                uint32_t offset = 0;
                const mx::VariableBlock &pub = pixel.getUniformBlock(mx::HW::PUBLIC_UNIFORMS);
                for (size_t i = 0; i < pub.size(); ++i)
                {
                    const mx::ShaderPort *port = pub[i];

                    PeMaterialInput in;
                    in.var = port->getVariable();
                    in.type = gen->getSyntax().getTypeName(port->getType());

                    const std::string path = port->getPath();
                    const size_t slash = path.rfind('/');
                    in.input = slash == std::string::npos ? path : path.substr(slash + 1);
                    in.node = slash == std::string::npos ? std::string() : path.substr(0, slash);
                    in.value = port->getValue() ? port->getValue()->getValueString() : std::string();
                    if (in.type == "SamplerTexture2D")
                        in.file = JsonFile(doc, path, mtlx);

                    const uint32_t size = PeTypeSize(in.type);
                    if (size == 0)
                    {
                        dropped.push_back(in.var);
                        continue;
                    }

                    if (in.node.empty())
                    {
                        const mx::ShaderOutput *socket = graph.getInputSocket(port->getName());
                        if (!socket || socket->getConnections().empty())
                            throw std::runtime_error(name + ": no node identity for uniform '" + in.var + "'");

                        const mx::ShaderInput *input = socket->getConnections().front();
                        in.node = input->getNode()->getName();
                        in.input = input->getName();
                    }

                    const auto [identity, first] = identities.emplace(std::make_pair(in.node, in.input), inputs.size());
                    if (first)
                    {
                        in.offset = offset;
                        offset += size;
                    }
                    else
                    {
                        // MaterialX can publish a shared constant once per consumer; every alias reads its one value.
                        const auto &canonical = inputs[identity->second];
                        if (canonical.type != in.type || canonical.value != in.value || canonical.file != in.file)
                            throw std::runtime_error(name + ": conflicting aliases for " + in.node + "." + in.input);

                        in.offset = canonical.offset;
                        in.alias = true;
                    }
                    inputs.push_back(in);
                }

                // --- Engine-owned private uniforms. ---
                bool refractionTwoSided = false;
                const mx::VariableBlock &priv = pixel.getUniformBlock(mx::HW::PRIVATE_UNIFORMS);
                for (size_t i = 0; i < priv.size(); ++i)
                {
                    const std::string var = priv[i]->getVariable();
                    if (var == "u_refractionTwoSided")
                        refractionTwoSided = true;
                    else if (var != "u_viewPosition")
                        dropped.push_back(var);
                }

                // --- Replace the uniform block with statics. ---
                const auto [cbStart, cbEnd] = FindBlock(text, "cbuffer pixelCB");
                if (cbStart == std::string::npos)
                {
                    error = name + ": generated pixel stage has no 'cbuffer pixelCB' block";
                    return false;
                }

                std::string statics;
                for (const PeMaterialInput &in : inputs)
                    statics += "static " + in.type + " " + in.var + ";\n";
                if (refractionTwoSided)
                    statics += "static const bool u_refractionTwoSided = PE_REFRACTION_TWO_SIDED;\n";

                text.replace(cbStart, cbEnd - cbStart + 1, statics);
                text = std::regex_replace(text, std::regex("\\bu_viewPosition\\b"), "pe_view_position()");
                if (!AddDerivativeSeams(text, name, error))
                    return false;

                for (const std::string &var : dropped)
                {
                    if (References(text, var))
                    {
                        error = name + ": '" + var + "' is still referenced after the ABI transform (unsupported input or override missing)";
                        return false;
                    }
                }

                // --- The fragment entry becomes a function the resolve calls. ---
                const std::regex entry(R"(\[shader\("fragment"\)\]\s*float4\s+fragmentMain\s*\(\s*VertexData\s+_vd\s*\)\s*:\s*SV_Target)");
                if (!std::regex_search(text, entry))
                {
                    error = name + ": fragmentMain signature not found";
                    return false;
                }

                text = std::regex_replace(text, entry, "float4 pe_material_fragment(VertexData _vd)");
                CaptureThinInputs(text, *surfaceNode, ctx);

                // --- Loader, vertex data, shade, thin values. ---
                std::string tail = "\nvoid pe_load_material(ByteAddressBuffer b, uint o)\n{\n";
                for (const PeMaterialInput &in : inputs)
                {
                    for (const std::string &line : LoaderLines(in))
                        tail += "    " + line + "\n";
                }
                tail += "}\n\nvoid pe_fill_vertex_data(PeSurfacePoint sp)\n{\n";

                if (text.find("SV_Position : SV_Position") != std::string::npos)
                    tail += "    vd.SV_Position = sp.svPosition;\n";

                std::vector<std::string> vertexData;
                const mx::VariableBlock &vdBlock = pixel.getInputBlock(mx::HW::VERTEX_DATA);
                for (size_t i = 0; i < vdBlock.size(); ++i)
                {
                    const std::string var = vdBlock[i]->getVariable();
                    const std::string type = gen->getSyntax().getTypeName(vdBlock[i]->getType());

                    if (var.starts_with("texcoord_") && var != "texcoord_0")
                        throw std::runtime_error(name + ": UV-set index other than 0 is unsupported ('" + var + "')");

                    static const std::map<std::string, std::string> kSource = {
                        {"positionWorld", "sp.P"},
                        {"normalWorld", "sp.N"},
                        {"tangentWorld", "sp.T"},
                        {"bitangentWorld", "sp.B"},
                        {"texcoord_0", "sp.uv0"},
                        {"color_0", "sp.color0"},
                    };
                    auto it = kSource.find(var);
                    if (it == kSource.end() || (var == "texcoord_0" && type != "float2"))
                    {
                        error = name + ": unsupported vertex data '" + var + "' (" + type + ")";
                        return false;
                    }

                    const std::string swizzle = var == "color_0" && type == "float3" ? ".rgb" : "";
                    tail += "    vd." + var + " = " + it->second + swizzle + ";\n";
                    vertexData.push_back(var);
                }
                tail += "}\n\n" + SurfaceFrameEvaluator(
                                      std::find(vertexData.begin(), vertexData.end(), "texcoord_0") != vertexData.end());
                tail += "float pe_material_mask(PeSurfacePoint sp, float2 nextUv, float blend)\n"
                        "{\n"
                        "    pe_fill_vertex_data(sp);\n"
                        "    return pe_material_inputs(nextUv, blend).geometry_opacity;\n"
                        "}\n\n";
                tail += "#ifndef PE_MATERIAL_THIN\n"
                        "float4 pe_material_shade(float2 nextUv, float blend)\n{\n"
                        "    return pe_shade_inputs(pe_material_inputs(nextUv, blend));\n}\n\n"
                        "float4 pe_material_shade()\n{\n"
                        "    return pe_material_shade(float2(0.0, 0.0), 0.0);\n}\n#endif\n";

                tail += "\nstruct PeThin\n"
                        "{\n"
                        "    float3 normal;\n"
                        "    float3 albedo;\n"
                        "    float roughness;\n"
                        "    float metalness;\n"
                        "};\n"
                        "\n"
                        "PeThin pe_material_thin(PeSurfacePoint sp, float2 nextUv, float blend)\n"
                        "{\n"
                        "    pe_fill_vertex_data(sp);\n"
                        "    const PeSurfaceInputs p = pe_material_inputs(nextUv, blend);\n"
                        "\n"
                        "    PeThin t;\n"
                        "    t.normal = normalize(p.geometry_normal);\n"
                        "    t.albedo = p.base_color * p.base_weight;\n"
                        "    t.roughness = p.specular_roughness;\n"
                        "    t.metalness = p.base_metalness;\n"
                        "    return t;\n"
                        "}\n\n"
                        "PeThin pe_material_thin(PeSurfacePoint sp)\n{\n"
                        "    return pe_material_thin(sp, float2(0.0, 0.0), 0.0);\n}\n";
                text = "#define PE_OPENPBR_MODULE_ABI " + std::to_string(kOpenPBRModuleAbi) + "\n" + text + tail;

                std::string json = "{\"module_abi\":" + std::to_string(kOpenPBRModuleAbi) + ",\"material\":\"" + name +
                                   "\",\"surface\":\"" + surface->getNamePath() + "\",\"inputs\":[";
                bool firstInput = true;
                for (const PeMaterialInput &in : inputs)
                {
                    if (in.alias)
                        continue;

                    json += std::string(firstInput ? "" : ",") +
                            "{\"name\":\"" + in.var +
                            "\",\"input\":\"" + in.input +
                            "\",\"node\":\"" + in.node +
                            "\",\"type\":\"" + in.type +
                            "\",\"offset\":" + std::to_string(in.offset) +
                            ",\"default\":" + JsonDefault(in);
                    if (in.type == "SamplerTexture2D")
                        json += ",\"file\":" + in.file;

                    json += "}";
                    firstInput = false;
                }
                json += "],\"vertexData\":[";
                for (size_t i = 0; i < vertexData.size(); ++i)
                    json += std::string(i ? "," : "") + "\"" + vertexData[i] + "\"";
                json += "],\"byteSize\":" + std::to_string(offset) + ",\"thin\":true}\n";

                // --- Write both files. ---
                const std::filesystem::path outputs[] = {outDir / (name + ".json"), outDir / (name + ".slang")};
                const std::filesystem::path temps[] = {outDir / (name + ".json.tmp"), outDir / (name + ".slang.tmp")};
                const std::string *contents[] = {&json, &text};
                const auto backup = outDir / (name + ".json.bak");
                auto fail = [&](const std::filesystem::path &file)
                {
                    error = "MaterialX: failed to write " + file.string();
                    std::error_code ignored;
                    for (const auto &tmp : temps)
                        std::filesystem::remove(tmp, ignored);
                    std::filesystem::remove(backup, ignored);
                    return false;
                };

                for (size_t i = 0; i < std::size(outputs); ++i)
                {
                    std::ofstream file(temps[i], std::ios::binary);
                    file << *contents[i];
                    file.close();
                    if (!file)
                        return fail(temps[i]);
                }

                std::error_code ec;
                const bool hadJson = std::filesystem::exists(outputs[0], ec);
                if (ec)
                    return fail(outputs[0]);

                // Keep the first output recoverable if replacing the second one fails.
                if (hadJson)
                {
                    std::filesystem::copy_file(outputs[0], backup, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                        return fail(outputs[0]);
                }

                std::filesystem::rename(temps[0], outputs[0], ec);
                if (ec)
                    return fail(outputs[0]);

                std::filesystem::rename(temps[1], outputs[1], ec);
                if (ec)
                {
                    std::error_code rollback;
                    if (hadJson)
                        std::filesystem::rename(backup, outputs[0], rollback);
                    else
                        std::filesystem::remove(outputs[0], rollback);

                    if (rollback)
                    {
                        error = "MaterialX: failed to write " + outputs[1].string() + "; failed to restore " + outputs[0].string();
                        for (const auto &tmp : temps)
                            std::filesystem::remove(tmp, rollback);
                        return false; // Keep the backup for recovery.
                    }

                    return fail(outputs[1]);
                }

                std::filesystem::remove(backup, ec);
            }

            return true;
        }
        catch (const std::exception &e)
        {
            error = std::string("MaterialX: ") + e.what();
            return false;
        }
    }
} // namespace pe
