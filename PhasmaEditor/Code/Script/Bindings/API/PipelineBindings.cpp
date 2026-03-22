#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::PrimitiveTopology> s_topologyMap = {
        {"point_list", vk::PrimitiveTopology::ePointList},
        {"line_list", vk::PrimitiveTopology::eLineList},
        {"line_strip", vk::PrimitiveTopology::eLineStrip},
        {"triangle_list", vk::PrimitiveTopology::eTriangleList},
        {"triangle_strip", vk::PrimitiveTopology::eTriangleStrip},
        {"triangle_fan", vk::PrimitiveTopology::eTriangleFan},
        {"patch_list", vk::PrimitiveTopology::ePatchList},
    };

    static const std::unordered_map<std::string_view, vk::PolygonMode> s_polygonModeMap = {
        {"fill", vk::PolygonMode::eFill},
        {"line", vk::PolygonMode::eLine},
        {"point", vk::PolygonMode::ePoint},
    };

    static const std::unordered_map<std::string_view, vk::CullModeFlagBits> s_cullModeMap = {
        {"none", vk::CullModeFlagBits::eNone},
        {"front", vk::CullModeFlagBits::eFront},
        {"back", vk::CullModeFlagBits::eBack},
        {"front_and_back", vk::CullModeFlagBits::eFrontAndBack},
    };

    static const std::unordered_map<std::string_view, vk::CompareOp> s_compareOpMap = {
        {"never", vk::CompareOp::eNever},
        {"less", vk::CompareOp::eLess},
        {"equal", vk::CompareOp::eEqual},
        {"less_equal", vk::CompareOp::eLessOrEqual},
        {"greater", vk::CompareOp::eGreater},
        {"not_equal", vk::CompareOp::eNotEqual},
        {"greater_equal", vk::CompareOp::eGreaterOrEqual},
        {"always", vk::CompareOp::eAlways},
    };

    static const std::unordered_map<std::string_view, vk::StencilOp> s_stencilOpMap = {
        {"keep", vk::StencilOp::eKeep},
        {"zero", vk::StencilOp::eZero},
        {"replace", vk::StencilOp::eReplace},
        {"increment_clamp", vk::StencilOp::eIncrementAndClamp},
        {"decrement_clamp", vk::StencilOp::eDecrementAndClamp},
        {"invert", vk::StencilOp::eInvert},
        {"increment_wrap", vk::StencilOp::eIncrementAndWrap},
        {"decrement_wrap", vk::StencilOp::eDecrementAndWrap},
    };

    static const std::unordered_map<std::string_view, vk::DynamicState> s_dynamicStateMap = {
        {"viewport", vk::DynamicState::eViewport},
        {"scissor", vk::DynamicState::eScissor},
        {"line_width", vk::DynamicState::eLineWidth},
        {"depth_bias", vk::DynamicState::eDepthBias},
        {"blend_constants", vk::DynamicState::eBlendConstants},
        {"depth_bounds", vk::DynamicState::eDepthBounds},
        {"stencil_compare_mask", vk::DynamicState::eStencilCompareMask},
        {"stencil_write_mask", vk::DynamicState::eStencilWriteMask},
        {"stencil_reference", vk::DynamicState::eStencilReference},
        {"cull_mode", vk::DynamicState::eCullMode},
        {"front_face", vk::DynamicState::eFrontFace},
        {"primitive_topology", vk::DynamicState::ePrimitiveTopology},
        {"depth_test_enable", vk::DynamicState::eDepthTestEnable},
        {"depth_write_enable", vk::DynamicState::eDepthWriteEnable},
        {"depth_compare_op", vk::DynamicState::eDepthCompareOp},
        {"stencil_test_enable", vk::DynamicState::eStencilTestEnable},
    };

    // Reverse lookups for getters
    static std::string TopologyToString(vk::PrimitiveTopology t)
    {
        for (auto &[k, v] : s_topologyMap)
            if (v == t)
                return std::string(k);
        return "unknown";
    }

    static std::string PolygonModeToString(vk::PolygonMode m)
    {
        for (auto &[k, v] : s_polygonModeMap)
            if (v == m)
                return std::string(k);
        return "unknown";
    }

    static std::string CullModeToString(vk::CullModeFlags f)
    {
        for (auto &[k, v] : s_cullModeMap)
            if (vk::CullModeFlags(v) == f)
                return std::string(k);
        return "unknown";
    }

    static std::string CompareOpToString(vk::CompareOp o)
    {
        for (auto &[k, v] : s_compareOpMap)
            if (v == o)
                return std::string(k);
        return "unknown";
    }

    static std::string StencilOpToString(vk::StencilOp o)
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
                        pi.pVertShader = Shader::Create(
                            Path::Assets + path, vk::ShaderStageFlagBits::eVertex,
                            entry, std::vector<Define>{}, ShaderCodeType::HLSL);
                    });

                // pFragShader
                piType["get_fragment_shader"] = [](PassInfo &pi) -> Shader * { return pi.pFragShader; };
                piType["set_fragment_shader"] = sol::overload(
                    [](PassInfo &pi, Shader *s) { pi.pFragShader = s; },
                    [](PassInfo &pi, const std::string &path, const std::string &entry) {
                        Shader::Destroy(pi.pFragShader);
                        pi.pFragShader = Shader::Create(
                            Path::Assets + path, vk::ShaderStageFlagBits::eFragment,
                            entry, std::vector<Define>{}, ShaderCodeType::HLSL);
                    });

                // pCompShader
                piType["get_compute_shader"] = [](PassInfo &pi) -> Shader * { return pi.pCompShader; };
                piType["set_compute_shader"] = sol::overload(
                    [](PassInfo &pi, Shader *s) { pi.pCompShader = s; },
                    [](PassInfo &pi, const std::string &path, const std::string &entry) {
                        Shader::Destroy(pi.pCompShader);
                        pi.pCompShader = Shader::Create(
                            Path::Assets + path, vk::ShaderStageFlagBits::eCompute,
                            entry, std::vector<Define>{}, ShaderCodeType::HLSL);
                    });

                // topology
                piType["get_topology"] = [](PassInfo &pi) -> std::string { return TopologyToString(pi.topology); };
                piType["set_topology"] = [](PassInfo &pi, const std::string &topo) {
                    pi.topology = Lookup(topo, s_topologyMap, vk::PrimitiveTopology::eTriangleList);
                };

                // polygonMode
                piType["get_polygon_mode"] = [](PassInfo &pi) -> std::string { return PolygonModeToString(pi.polygonMode); };
                piType["set_polygon_mode"] = [](PassInfo &pi, const std::string &mode) {
                    pi.polygonMode = Lookup(mode, s_polygonModeMap, vk::PolygonMode::eFill);
                };

                // cullMode
                piType["get_cull_mode"] = [](PassInfo &pi) -> std::string { return CullModeToString(pi.cullMode); };
                piType["set_cull_mode"] = [](PassInfo &pi, const std::string &mode) {
                    pi.cullMode = Lookup(mode, s_cullModeMap, vk::CullModeFlagBits::eBack);
                };

                // lineWidth
                piType["get_line_width"] = sol::property([](PassInfo &pi) -> float { return pi.lineWidth; });
                piType["set_line_width"] = [](PassInfo &pi, float w) { pi.lineWidth = w; };

                // blendEnable
                piType["is_blend_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.blendEnable; });
                piType["set_blend_enable"] = [](PassInfo &pi, bool e) { pi.blendEnable = e; };

                // colorBlendAttachments (via preset modes)
                piType["set_blend_mode"] = [](PassInfo &pi, const std::string &mode) {
                    if (mode == "additive") pi.colorBlendAttachments = {PipelineColorBlendAttachmentState::AdditiveColor};
                    else if (mode == "alpha") pi.colorBlendAttachments = {PipelineColorBlendAttachmentState::TransparencyBlend};
                    else if (mode == "particles") pi.colorBlendAttachments = {PipelineColorBlendAttachmentState::ParticlesBlend};
                    else pi.colorBlendAttachments = {PipelineColorBlendAttachmentState::Default};
                };

                // dynamicStates
                piType["get_dynamic_states"] = [&lua](PassInfo &pi) -> sol::table {
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
                    pi.depthCompareOp = Lookup(op, s_compareOpMap, vk::CompareOp::eLess);
                };

                // stencilTestEnable
                piType["is_stencil_test_enabled"] = sol::property([](PassInfo &pi) -> bool { return pi.stencilTestEnable; });
                piType["set_stencil_test"] = [](PassInfo &pi, bool e) { pi.stencilTestEnable = e; };

                // stencilFailOp, stencilPassOp, stencilDepthFailOp, stencilCompareOp
                piType["get_stencil_ops"] = [&lua](PassInfo &pi) -> sol::table {
                    sol::table t = lua.create_table();
                    t["fail"] = StencilOpToString(pi.stencilFailOp);
                    t["pass"] = StencilOpToString(pi.stencilPassOp);
                    t["depth_fail"] = StencilOpToString(pi.stencilDepthFailOp);
                    t["compare"] = CompareOpToString(pi.stencilCompareOp);
                    return t;
                };
                piType["set_stencil_ops"] = [](PassInfo &pi, const std::string &fail, const std::string &pass, const std::string &depthFail, const std::string &compare) {
                    pi.stencilFailOp = Lookup(fail, s_stencilOpMap, vk::StencilOp::eKeep);
                    pi.stencilPassOp = Lookup(pass, s_stencilOpMap, vk::StencilOp::eKeep);
                    pi.stencilDepthFailOp = Lookup(depthFail, s_stencilOpMap, vk::StencilOp::eKeep);
                    pi.stencilCompareOp = Lookup(compare, s_compareOpMap, vk::CompareOp::eAlways);
                };

                // stencilCompareMask, stencilWriteMask, stencilReference
                piType["get_stencil_masks"] = [&lua](PassInfo &pi) -> sol::table {
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
