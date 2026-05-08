#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeTopology> s_topologyMap = {
        {"point_list", PE_TOPOLOGY_POINT_LIST},
        {"line_list", PE_TOPOLOGY_LINE_LIST},
        {"line_strip", PE_TOPOLOGY_LINE_STRIP},
        {"triangle_list", PE_TOPOLOGY_TRIANGLE_LIST},
        {"triangle_strip", PE_TOPOLOGY_TRIANGLE_STRIP},
        {"triangle_fan", PE_TOPOLOGY_TRIANGLE_FAN},
        {"patch_list", PE_TOPOLOGY_PATCH_LIST},
    };

    static const std::unordered_map<std::string_view, PePolygonMode> s_polygonModeMap = {
        {"fill", PE_POLYGON_MODE_FILL},
        {"line", PE_POLYGON_MODE_LINE},
        {"point", PE_POLYGON_MODE_POINT},
    };

    static const std::unordered_map<std::string_view, PeCullMode> s_cullModeMap = {
        {"none", PE_CULL_MODE_NONE},
        {"front", PE_CULL_MODE_FRONT},
        {"back", PE_CULL_MODE_BACK},
        {"front_and_back", PE_CULL_MODE_FRONT_AND_BACK},
    };

    static const std::unordered_map<std::string_view, PeCompareOp> s_compareOpMap = {
        {"never", PE_COMPARE_OP_NEVER},
        {"less", PE_COMPARE_OP_LESS},
        {"equal", PE_COMPARE_OP_EQUAL},
        {"less_equal", PE_COMPARE_OP_LESS_OR_EQUAL},
        {"greater", PE_COMPARE_OP_GREATER},
        {"not_equal", PE_COMPARE_OP_NOT_EQUAL},
        {"greater_equal", PE_COMPARE_OP_GREATER_OR_EQUAL},
        {"always", PE_COMPARE_OP_ALWAYS},
    };

    static const std::unordered_map<std::string_view, PeStencilOp> s_stencilOpMap = {
        {"keep", PE_STENCIL_OP_KEEP},
        {"zero", PE_STENCIL_OP_ZERO},
        {"replace", PE_STENCIL_OP_REPLACE},
        {"increment_clamp", PE_STENCIL_OP_INCREMENT_AND_CLAMP},
        {"decrement_clamp", PE_STENCIL_OP_DECREMENT_AND_CLAMP},
        {"invert", PE_STENCIL_OP_INVERT},
        {"increment_wrap", PE_STENCIL_OP_INCREMENT_AND_WRAP},
        {"decrement_wrap", PE_STENCIL_OP_DECREMENT_AND_WRAP},
    };

    static const std::unordered_map<std::string_view, PeDynamicState> s_dynamicStateMap = {
        {"viewport", PE_DYNAMIC_STATE_VIEWPORT},
        {"scissor", PE_DYNAMIC_STATE_SCISSOR},
        {"line_width", PE_DYNAMIC_STATE_LINE_WIDTH},
        {"depth_bias", PE_DYNAMIC_STATE_DEPTH_BIAS},
        {"blend_constants", PE_DYNAMIC_STATE_BLEND_CONSTANTS},
        {"depth_bounds", PE_DYNAMIC_STATE_DEPTH_BOUNDS},
        {"stencil_compare_mask", PE_DYNAMIC_STATE_STENCIL_COMPARE_MASK},
        {"stencil_write_mask", PE_DYNAMIC_STATE_STENCIL_WRITE_MASK},
        {"stencil_reference", PE_DYNAMIC_STATE_STENCIL_REFERENCE},
        {"cull_mode", PE_DYNAMIC_STATE_CULL_MODE},
        {"front_face", PE_DYNAMIC_STATE_FRONT_FACE},
        {"primitive_topology", PE_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY},
        {"depth_test_enable", PE_DYNAMIC_STATE_DEPTH_TEST_ENABLE},
        {"depth_write_enable", PE_DYNAMIC_STATE_DEPTH_WRITE_ENABLE},
        {"depth_compare_op", PE_DYNAMIC_STATE_DEPTH_COMPARE_OP},
        {"stencil_test_enable", PE_DYNAMIC_STATE_STENCIL_TEST_ENABLE},
    };

    // Reverse lookups for getters
    static std::string TopologyToString(PeTopology t)
    {
        for (auto &[k, v] : s_topologyMap)
            if (v == t)
                return std::string(k);
        return "unknown";
    }

    static std::string PolygonModeToString(PePolygonMode m)
    {
        for (auto &[k, v] : s_polygonModeMap)
            if (v == m)
                return std::string(k);
        return "unknown";
    }

    static std::string CullModeToString(PeCullMode f)
    {
        for (auto &[k, v] : s_cullModeMap)
            if (v == f)
                return std::string(k);
        return "unknown";
    }

    static std::string CompareOpToString(PeCompareOp o)
    {
        for (auto &[k, v] : s_compareOpMap)
            if (v == o)
                return std::string(k);
        return "unknown";
    }

    static std::string StencilOpToString(PeStencilOp o)
    {
        for (auto &[k, v] : s_stencilOpMap)
            if (v == o)
                return std::string(k);
        return "unknown";
    }

    static struct PipelineBindings
    {
        PipelineBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // --- HitGroup ---
                sol::usertype<HitGroup> hitGroupType = lua.new_usertype<HitGroup>("HitGroup",
                    sol::constructors<HitGroup()>());
                hitGroupType["closest_hit"] = sol::property(
                    [](HitGroup &hg) -> Shader * { return hg.closestHit; },
                    [](HitGroup &hg, Shader *s) { hg.closestHit = s; });
                hitGroupType["any_hit"] = sol::property(
                    [](HitGroup &hg) -> Shader * { return hg.anyHit; },
                    [](HitGroup &hg, Shader *s) { hg.anyHit = s; });
                hitGroupType["intersection"] = sol::property(
                    [](HitGroup &hg) -> Shader * { return hg.intersection; },
                    [](HitGroup &hg, Shader *s) { hg.intersection = s; });

                // --- Acceleration ---
                sol::usertype<Acceleration> accelType = lua.new_usertype<Acceleration>("Acceleration",
                    sol::constructors<Acceleration()>());
                accelType["ray_gen"] = sol::property(
                    [](Acceleration &a) -> Shader * { return a.rayGen; },
                    [](Acceleration &a, Shader *s) { a.rayGen = s; });
                accelType["miss"] = sol::property(
                    [](Acceleration &a) -> sol::as_table_t<std::vector<Shader *>> { return sol::as_table(a.miss); },
                    [](Acceleration &a, const sol::table &t) {
                        a.miss.clear();
                        for (auto &[_, v] : t)
                            if (v.is<Shader *>()) a.miss.push_back(v.as<Shader *>());
                    });
                accelType["hit_groups"] = sol::property(
                    [](Acceleration &a) -> sol::as_table_t<std::vector<HitGroup>> { return sol::as_table(a.hitGroups); },
                    [](Acceleration &a, const sol::table &t) {
                        a.hitGroups.clear();
                        for (auto &[_, v] : t)
                            if (v.is<HitGroup>()) a.hitGroups.push_back(v.as<HitGroup>());
                    });
                accelType["max_recursion_depth"] = sol::property(
                    [](Acceleration &a) -> uint32_t { return a.maxRecursionDepth; },
                    [](Acceleration &a, uint32_t d) { a.maxRecursionDepth = d; });

                // --- PassInfo ---
                sol::usertype<PassInfo> piType = lua.new_usertype<PassInfo>("PassInfo", sol::no_constructor);

                // Factory
                lua.set_function("create_pass_info", []() -> PassInfo * {
                    return new PassInfo();
                });
                lua.set_function("destroy_pass_info", [](PassInfo *pi) {
                    delete pi;
                });

                // Update
                piType["update"] = [](PassInfo &pi) { pi.Update(); };

                // GetDescriptors
                piType["get_descriptors"] = [](PassInfo &pi, uint32_t frame) -> sol::as_table_t<std::vector<Descriptor *>> {
                    return sol::as_table(std::vector<Descriptor *>(pi.GetDescriptors(frame).begin(), pi.GetDescriptors(frame).end()));
                };

                // pVertShader
                piType["get_vertex_shader"] = [](PassInfo &pi) -> Shader * { return pi.pVertShader; };
                piType["set_vertex_shader"] = sol::overload(
                    [](PassInfo &pi, Shader *s) { pi.pVertShader = s; },
                    [](PassInfo &pi, const std::string &path, const std::string &entry) {
                        Shader::Destroy(pi.pVertShader);
                        pi.pVertShader = Shader::Create({.sourcePath = Path::Assets + path,
                                                         .entryPoint = entry,
                                                         .stage = PE_SHADER_STAGE_VERTEX});
                    });

                // pFragShader
                piType["get_fragment_shader"] = [](PassInfo &pi) -> Shader * { return pi.pFragShader; };
                piType["set_fragment_shader"] = sol::overload(
                    [](PassInfo &pi, Shader *s) { pi.pFragShader = s; },
                    [](PassInfo &pi, const std::string &path, const std::string &entry) {
                        Shader::Destroy(pi.pFragShader);
                        pi.pFragShader = Shader::Create({.sourcePath = Path::Assets + path,
                                                         .entryPoint = entry,
                                                         .stage = PE_SHADER_STAGE_FRAGMENT});
                    });

                // pCompShader
                piType["get_compute_shader"] = [](PassInfo &pi) -> Shader * { return pi.pCompShader; };
                piType["set_compute_shader"] = sol::overload(
                    [](PassInfo &pi, Shader *s) { pi.pCompShader = s; },
                    [](PassInfo &pi, const std::string &path, const std::string &entry) {
                        Shader::Destroy(pi.pCompShader);
                        pi.pCompShader = Shader::Create({.sourcePath = Path::Assets + path,
                                                         .entryPoint = entry,
                                                         .stage = PE_SHADER_STAGE_COMPUTE});
                    });

                // topology
                piType["get_topology"] = [](PassInfo &pi) -> std::string { return TopologyToString(pi.topology); };
                piType["set_topology"] = [](PassInfo &pi, const std::string &topo) {
                    pi.topology = Lookup(topo, s_topologyMap, PE_TOPOLOGY_TRIANGLE_LIST);
                };

                // polygonMode
                piType["get_polygon_mode"] = [](PassInfo &pi) -> std::string { return PolygonModeToString(pi.polygonMode); };
                piType["set_polygon_mode"] = [](PassInfo &pi, const std::string &mode) {
                    pi.polygonMode = Lookup(mode, s_polygonModeMap, PE_POLYGON_MODE_FILL);
                };

                // cullMode
                piType["get_cull_mode"] = [](PassInfo &pi) -> std::string { return CullModeToString(pi.cullMode); };
                piType["set_cull_mode"] = [](PassInfo &pi, const std::string &mode) {
                    pi.cullMode = Lookup(mode, s_cullModeMap, PE_CULL_MODE_BACK);
                };

                // lineWidth
                piType["get_line_width"] = sol::property([](PassInfo &pi) -> float { return pi.lineWidth; });
                piType["set_line_width"] = [](PassInfo &pi, float w) { pi.lineWidth = w; };

                // blendEnable
                piType["is_blend_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.blendEnable; });
                piType["set_blend_enable"] = [](PassInfo &pi, bool e) { pi.blendEnable = e; };

                // colorBlendAttachments (via preset modes)
                piType["set_blend_mode"] = [](PassInfo &pi, const std::string &mode) {
                    if (mode == "additive") pi.colorBlendAttachments = {BlendState::AdditiveColor};
                    else if (mode == "alpha") pi.colorBlendAttachments = {BlendState::TransparencyBlend};
                    else if (mode == "particles") pi.colorBlendAttachments = {BlendState::ParticlesBlend};
                    else pi.colorBlendAttachments = {BlendState::Default};
                };

                // dynamicStates
                piType["get_dynamic_states"] = [](PassInfo &pi, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    for (size_t i = 0; i < pi.dynamicStates.size(); i++)
                    {
                        for (auto &[k, v] : s_dynamicStateMap)
                        {
                            if (v == pi.dynamicStates[i])
                            {
                                t[i + 1] = std::string(k);
                                break;
                            }
                        }
                    }
                    return t;
                };
                piType["set_dynamic_states"] = [](PassInfo &pi, const sol::table &states) {
                    pi.dynamicStates.clear();
                    for (auto &[_, v] : states)
                    {
                        if (v.is<std::string>())
                        {
                            auto it = s_dynamicStateMap.find(std::string_view(v.as<std::string>()));
                            if (it != s_dynamicStateMap.end())
                                pi.dynamicStates.push_back(it->second);
                        }
                    }
                };

                // colorFormats
                piType["set_color_format"] = [](PassInfo &pi, std::shared_ptr<LuaImage> img) {
                    if (!img) return;
                    Image *p = img->Get();
                    if (p) pi.colorFormats = {p->GetFormat()};
                };
                piType["set_color_formats"] = [](PassInfo &pi, const sol::table &images) {
                    pi.colorFormats.clear();
                    for (auto &[_, v] : images)
                    {
                        if (v.is<std::shared_ptr<LuaImage>>())
                        {
                            Image *p = v.as<std::shared_ptr<LuaImage>>()->Get();
                            if (p) pi.colorFormats.push_back(p->GetFormat());
                        }
                    }
                };

                // depthFormat
                piType["set_depth_format"] = [](PassInfo &pi, std::shared_ptr<LuaImage> img) {
                    if (!img) return;
                    Image *p = img->Get();
                    if (p) pi.depthFormat = p->GetFormat();
                };

                // depthWriteEnable
                piType["is_depth_write_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.depthWriteEnable; });
                piType["set_depth_write"] = [](PassInfo &pi, bool e) { pi.depthWriteEnable = e; };

                // depthTestEnable
                piType["is_depth_test_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.depthTestEnable; });
                piType["set_depth_test"] = [](PassInfo &pi, bool e) { pi.depthTestEnable = e; };

                // depthCompareOp
                piType["get_depth_compare"] = [](PassInfo &pi) -> std::string { return CompareOpToString(pi.depthCompareOp); };
                piType["set_depth_compare"] = [](PassInfo &pi, const std::string &op) {
                    pi.depthCompareOp = Lookup(op, s_compareOpMap, PE_COMPARE_OP_LESS);
                };

                // stencilTestEnable
                piType["is_stencil_test_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.stencilTestEnable; });
                piType["set_stencil_test"] = [](PassInfo &pi, bool e) { pi.stencilTestEnable = e; };

                // stencilFailOp, stencilPassOp, stencilDepthFailOp, stencilCompareOp
                piType["get_stencil_ops"] = [](PassInfo &pi, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    t["fail"] = StencilOpToString(pi.stencilFailOp);
                    t["pass"] = StencilOpToString(pi.stencilPassOp);
                    t["depth_fail"] = StencilOpToString(pi.stencilDepthFailOp);
                    t["compare"] = CompareOpToString(pi.stencilCompareOp);
                    return t;
                };
                piType["set_stencil_ops"] = [](PassInfo &pi, const std::string &fail, const std::string &pass, const std::string &depthFail, const std::string &compare) {
                    pi.stencilFailOp = Lookup(fail, s_stencilOpMap, PE_STENCIL_OP_KEEP);
                    pi.stencilPassOp = Lookup(pass, s_stencilOpMap, PE_STENCIL_OP_KEEP);
                    pi.stencilDepthFailOp = Lookup(depthFail, s_stencilOpMap, PE_STENCIL_OP_KEEP);
                    pi.stencilCompareOp = Lookup(compare, s_compareOpMap, PE_COMPARE_OP_ALWAYS);
                };

                // stencilCompareMask, stencilWriteMask, stencilReference
                piType["get_stencil_masks"] = [](PassInfo &pi, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    t["compare_mask"] = pi.stencilCompareMask;
                    t["write_mask"] = pi.stencilWriteMask;
                    t["reference"] = pi.stencilReference;
                    return t;
                };
                piType["set_stencil_masks"] = [](PassInfo &pi, uint32_t compareMask, uint32_t writeMask, uint32_t reference) {
                    pi.stencilCompareMask = compareMask;
                    pi.stencilWriteMask = writeMask;
                    pi.stencilReference = reference;
                };

                // acceleration
                piType["get_acceleration"] = [](PassInfo &pi) -> Acceleration & { return pi.acceleration; };
                piType["set_acceleration"] = [](PassInfo &pi, const Acceleration &a) { pi.acceleration = a; };

                // name
                piType["get_name"] = sol::property([](PassInfo &pi) -> const std::string & { return pi.name; });
                piType["set_name"] = [](PassInfo &pi, const std::string &n) { pi.name = n; };

                // --- Pipeline ---
                sol::usertype<Pipeline> pipeType = lua.new_usertype<Pipeline>("Pipeline", sol::no_constructor);

                // GetInfo
                pipeType["get_info"] = [](Pipeline &p) -> PassInfo & { return p.GetInfo(); }; });
        }
    } s_pipelineBindings;
} // namespace pe
