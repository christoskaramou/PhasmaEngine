#include "Script/ScriptSystem.h"
#include "API/Vertex.h"

namespace pe
{
    static struct VertexBindings
    {
        VertexBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // --- Vertex ---
                sol::usertype<Vertex> vertType = lua.new_usertype<Vertex>("Vertex",
                    sol::constructors<Vertex()>());

                // position
                vertType["get_position"] = [](Vertex &v) -> std::tuple<float, float, float> {
                    return {v.position[0], v.position[1], v.position[2]};
                };
                vertType["set_position"] = [](Vertex &v, float x, float y, float z) {
                    FillVertexPosition(v, x, y, z);
                };

                // uv
                vertType["get_uv"] = [](Vertex &v) -> std::tuple<float, float> {
                    return {v.uv[0], v.uv[1]};
                };
                vertType["set_uv"] = [](Vertex &v, float u, float vv) {
                    FillVertexUV(v, u, vv);
                };

                // normals
                vertType["get_normal"] = [](Vertex &v) -> std::tuple<float, float, float> {
                    return {v.normals[0], v.normals[1], v.normals[2]};
                };
                vertType["set_normal"] = [](Vertex &v, float x, float y, float z) {
                    FillVertexNormal(v, x, y, z);
                };

                // tangent
                vertType["get_tangent"] = [](Vertex &v) -> std::tuple<float, float, float, float> {
                    return {v.tangent[0], v.tangent[1], v.tangent[2], v.tangent[3]};
                };
                vertType["set_tangent"] = [](Vertex &v, float x, float y, float z, float w) {
                    FillVertexTangent(v, x, y, z, w);
                };

                // color
                vertType["get_color"] = [](Vertex &v) -> std::tuple<float, float, float, float> {
                    return {v.color[0], v.color[1], v.color[2], v.color[3]};
                };
                vertType["set_color"] = [](Vertex &v, float r, float g, float b, float a) {
                    FillVertexColor(v, r, g, b, a);
                };

                // joints + weights
                vertType["get_joints"] = [](Vertex &v) -> std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> {
                    return {v.joints[0], v.joints[1], v.joints[2], v.joints[3]};
                };
                vertType["get_weights"] = [](Vertex &v) -> std::tuple<float, float, float, float> {
                    return {v.weights[0], v.weights[1], v.weights[2], v.weights[3]};
                };
                vertType["set_joints_weights"] = [](Vertex &v, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                                     float w0, float w1, float w2, float w3) {
                    FillVertexJointsWeights(v, j0, j1, j2, j3, w0, w1, w2, w3);
                };

                // --- AabbVertex ---
                sol::usertype<AabbVertex> aabbType = lua.new_usertype<AabbVertex>("AabbVertex",
                    sol::constructors<AabbVertex()>());

                aabbType["get_position"] = [](AabbVertex &v) -> std::tuple<float, float, float> {
                    return {v.position[0], v.position[1], v.position[2]};
                };
                aabbType["set_position"] = [](AabbVertex &v, float x, float y, float z) {
                    v.position[0] = x; v.position[1] = y; v.position[2] = z;
                };

                // --- PositionUvVertex ---
                sol::usertype<PositionUvVertex> posUvType = lua.new_usertype<PositionUvVertex>("PositionUvVertex",
                    sol::constructors<PositionUvVertex()>());

                posUvType["get_position"] = [](PositionUvVertex &v) -> std::tuple<float, float, float> {
                    return {v.position[0], v.position[1], v.position[2]};
                };
                posUvType["set_position"] = [](PositionUvVertex &v, float x, float y, float z) {
                    FillVertexPosition(v, x, y, z);
                };

                posUvType["get_uv"] = [](PositionUvVertex &v) -> std::tuple<float, float> {
                    return {v.uv[0], v.uv[1]};
                };
                posUvType["set_uv"] = [](PositionUvVertex &v, float u, float vv) {
                    FillVertexUV(v, u, vv);
                };

                posUvType["get_joints"] = [](PositionUvVertex &v) -> std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> {
                    return {v.joints[0], v.joints[1], v.joints[2], v.joints[3]};
                };
                posUvType["get_weights"] = [](PositionUvVertex &v) -> std::tuple<float, float, float, float> {
                    return {v.weights[0], v.weights[1], v.weights[2], v.weights[3]};
                };
                posUvType["set_joints_weights"] = [](PositionUvVertex &v, uint8_t j0, uint8_t j1, uint8_t j2, uint8_t j3,
                                                      float w0, float w1, float w2, float w3) {
                    FillVertexJointsWeights(v, j0, j1, j2, j3, w0, w1, w2, w3);
                }; });
        }
    } s_vertexBindings;
} // namespace pe
