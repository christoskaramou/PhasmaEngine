-- Vertex API test suite (called from main.lua)

function run_vertex_tests()
    pe_log("=== Vertex API Tests ===")
    T.reset()

    -- Vertex
    local v = Vertex.new()
    T.check("Vertex constructor", v ~= nil)

    -- set_position / get_position
    v:set_position(1.0, 2.0, 3.0)
    local px, py, pz = v:get_position()
    T.check("set/get_position x", px == 1.0)
    T.check("set/get_position y", py == 2.0)
    T.check("set/get_position z", pz == 3.0)

    -- set_uv / get_uv
    v:set_uv(0.5, 0.75)
    local u, uv = v:get_uv()
    T.check("set/get_uv u", u == 0.5)
    T.check("set/get_uv v", uv == 0.75)

    -- set_normal / get_normal
    v:set_normal(0.0, 1.0, 0.0)
    local nx, ny, nz = v:get_normal()
    T.check("set/get_normal x", nx == 0.0)
    T.check("set/get_normal y", ny == 1.0)
    T.check("set/get_normal z", nz == 0.0)

    -- set_tangent / get_tangent
    v:set_tangent(1.0, 0.0, 0.0, 1.0)
    local tx, ty, tz, tw = v:get_tangent()
    T.check("set/get_tangent x", tx == 1.0)
    T.check("set/get_tangent y", ty == 0.0)
    T.check("set/get_tangent z", tz == 0.0)
    T.check("set/get_tangent w", tw == 1.0)

    -- set_color / get_color
    v:set_color(1.0, 0.5, 0.25, 1.0)
    local cr, cg, cb, ca = v:get_color()
    T.check("set/get_color r", cr == 1.0)
    T.check("set/get_color g", cg == 0.5)
    T.check("set/get_color b", cb == 0.25)
    T.check("set/get_color a", ca == 1.0)

    -- set_joints_weights / get_joints / get_weights
    v:set_joints_weights(0, 1, 2, 3, 0.5, 0.25, 0.15, 0.1)
    local j0, j1, j2, j3 = v:get_joints()
    T.check("get_joints j0", j0 == 0)
    T.check("get_joints j1", j1 == 1)
    T.check("get_joints j2", j2 == 2)
    T.check("get_joints j3", j3 == 3)
    local w0, w1, w2, w3 = v:get_weights()
    T.check("get_weights w0", w0 == 0.5)
    T.check("get_weights w1", w1 == 0.25)

    -- AabbVertex
    local aabb = AabbVertex.new()
    T.check("AabbVertex constructor", aabb ~= nil)
    aabb:set_position(10.0, 20.0, 30.0)
    local ax, ay, az = aabb:get_position()
    T.check("AabbVertex set/get_position x", ax == 10.0)
    T.check("AabbVertex set/get_position y", ay == 20.0)
    T.check("AabbVertex set/get_position z", az == 30.0)

    -- PositionUvVertex
    local puv = PositionUvVertex.new()
    T.check("PositionUvVertex constructor", puv ~= nil)
    puv:set_position(5.0, 6.0, 7.0)
    local ppx, ppy, ppz = puv:get_position()
    T.check("PositionUvVertex get_position x", ppx == 5.0)
    T.check("PositionUvVertex get_position y", ppy == 6.0)
    T.check("PositionUvVertex get_position z", ppz == 7.0)

    puv:set_uv(0.5, 0.75)
    local pu, pv = puv:get_uv()
    T.check("PositionUvVertex get_uv u", pu == 0.5)
    T.check("PositionUvVertex get_uv v", pv == 0.75)

    puv:set_joints_weights(4, 5, 6, 7, 0.5, 0.25, 0.125, 0.125)
    local pj0, pj1, pj2, pj3 = puv:get_joints()
    T.check("PositionUvVertex get_joints", pj0 == 4)
    local pw0, pw1, pw2, pw3 = puv:get_weights()
    T.check("PositionUvVertex get_weights", pw0 == 0.5)

    T.summary("Vertex API Tests")
end
