-- gamekit/build.lua — imperative scene CONSTRUCTION over scene.add_empty_node +
-- scene.attach_primitive. Turns "make a room / a row of pillars / a grid / a staircase /
-- scatter rocks" into ONE call, so an authoring script (or the AI agent via a single
-- execute_lua) builds a whole layout in one turn instead of placing primitives one at a time.
--
-- Pairs with gamekit/spatial.lua, which ARRANGES and PERCEIVES existing nodes (ring/grid/
-- place_on_ground/resolve_overlaps/decode_view/verify_visible); build CREATES them. Every
-- helper returns the node handle(s) it made, so you can immediately arrange, decode, verify,
-- or save them.
--
-- GROUPING + PREFABS: pass `parent` (a node handle) and every created node is parented under
-- it. Make a container with scene.add_empty_node, build into it, and you can save the whole
-- thing as a reusable .peprefab with scene.save_prefab(container, "Prefabs/x.peprefab") and
-- stamp copies later with scene.instantiate_prefab — the build → judge → save → reuse loop.
--
-- UNITS: a "cube" is the unit cube (1×1×1, centred on its node), so `size` is the object's
-- size in world units and `pos` is its centre. Positions are world-space (set_world_position
-- maps through any parent), so they stay correct whether or not you pass `parent`.

local build = {}

local _seq = 0
local function uid(prefix)
    _seq = _seq + 1
    return (prefix or "build") .. "_" .. _seq
end

local function v3(t, dx, dy, dz)
    if t then return t end
    return vec3(dx or 0.0, dy or 0.0, dz or 0.0)
end

-- Spawn one primitive (default "cube") as a node. opts:
--   { name, prim="cube", pos=vec3(0,0,0), size=vec3(1,1,1), rot=vec3(0,0,0), parent }
-- Returns the node handle (nil on failure).
function build.box(opts)
    opts = opts or {}
    local prim = opts.prim or "cube"
    local node = scene and scene.add_empty_node and scene.add_empty_node(opts.name or uid(prim))
    if not node then return nil end
    scene.attach_primitive(node, prim)
    if opts.parent and node.set_parent then node:set_parent(opts.parent) end
    node:set_world_position(v3(opts.pos))
    if opts.size and node.set_scale then node:set_scale(opts.size) end
    if opts.rot and node.set_rotation then node:set_rotation(opts.rot) end
    return node
end

-- Spawn `count` primitives evenly along the segment from `from` to `to` (inclusive).
-- opts: { from=vec3, to=vec3, count=2, prim, size, name, parent } -> array of handles.
function build.row(opts)
    opts = opts or {}
    local a, b = v3(opts.from), v3(opts.to)
    local n = math.max(1, opts.count or 2)
    local out = {}
    for i = 1, n do
        local f = (n == 1) and 0.0 or (i - 1) / (n - 1)
        out[i] = build.box({
            name = opts.name and (opts.name .. "_" .. i) or nil,
            prim = opts.prim, size = opts.size, parent = opts.parent,
            pos = vec3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f),
        })
    end
    return out
end

-- Spawn `count` primitives evenly around a circle on the XZ plane.
-- opts: { center=vec3, radius=5, count=8, y=center.y, prim, size, name, parent } -> handles.
function build.ring(opts)
    opts = opts or {}
    local c = v3(opts.center)
    local r = opts.radius or 5.0
    local n = math.max(1, opts.count or 8)
    local y = opts.y or c.y
    local out = {}
    for i = 1, n do
        local ang = (i - 1) * (2.0 * math.pi / n)
        out[i] = build.box({
            name = opts.name and (opts.name .. "_" .. i) or nil,
            prim = opts.prim, size = opts.size, parent = opts.parent,
            pos = vec3(c.x + math.cos(ang) * r, y, c.z + math.sin(ang) * r),
        })
    end
    return out
end

-- Spawn a row-major grid of primitives on the XZ plane.
-- opts: { origin=vec3, cols=4, rows=4, spacing=2, spacing_z=spacing, y=origin.y, prim, size,
--         name, parent } -> array of handles (row-major).
function build.grid(opts)
    opts = opts or {}
    local o = v3(opts.origin)
    local cols = math.max(1, opts.cols or 4)
    local rows = math.max(1, opts.rows or 4)
    local sx = opts.spacing or 2.0
    local sz = opts.spacing_z or sx
    local y = opts.y or o.y
    local out = {}
    for r = 0, rows - 1 do
        for c = 0, cols - 1 do
            out[#out + 1] = build.box({
                name = opts.name and (opts.name .. "_" .. (r * cols + c + 1)) or nil,
                prim = opts.prim, size = opts.size, parent = opts.parent,
                pos = vec3(o.x + c * sx, y, o.z + r * sz),
            })
        end
    end
    return out
end

-- A straight wall (a single scaled box) spanning `from`..`to` on the XZ plane, `height` tall
-- with its base at the from/to Y, `thickness` deep. opts: { from, to, height=3, thickness=0.2,
-- prim, name, parent }. Returns one handle (or nil if from==to).
function build.wall(opts)
    opts = opts or {}
    local a, b = v3(opts.from), v3(opts.to)
    local dx, dz = b.x - a.x, b.z - a.z
    local len = math.sqrt(dx * dx + dz * dz)
    if len < 1e-4 then return nil end
    local h = opts.height or 3.0
    local th = opts.thickness or 0.2
    return build.box({
        name = opts.name or uid("wall"), prim = opts.prim, parent = opts.parent,
        pos = vec3((a.x + b.x) * 0.5, a.y + h * 0.5, (a.z + b.z) * 0.5),
        size = vec3(th, h, len),
        rot = vec3(0.0, math.deg(math.atan(dx, dz)), 0.0), -- align +Z(length) onto the segment
    })
end

-- Build an axis-aligned room: floor + 4 walls + optional roof, with an optional doorway gap in
-- one wall. opts:
--   { center=vec3(0,0,0)  -- centre of the floor at floor-top height,
--     size=vec3(6,3,6)    -- interior footprint x, wall height y, footprint z,
--     thickness=0.2, floor=0.2, roof=false, prim="cube", name="room", parent,
--     doorway={ side="+z", width=1.5 } }  -- gap in one wall; side = -x|+x|-z|+z
-- Returns { floor=handle, roof=handle|nil, walls={handles...} }.
function build.room(opts)
    opts = opts or {}
    local c = v3(opts.center)
    local s = v3(opts.size, 6.0, 3.0, 6.0)
    local sx, h, sz = s.x, s.y, s.z
    local t = opts.thickness or 0.2
    local ft = opts.floor or 0.2
    local prim = opts.prim or "cube"
    local name = opts.name or "room"
    local par = opts.parent
    local door = opts.doorway
    local res = { walls = {} }

    res.floor = build.box({ name = name .. "_floor", prim = prim, parent = par,
        pos = vec3(c.x, c.y - ft * 0.5, c.z), size = vec3(sx + 2 * t, ft, sz + 2 * t) })
    if opts.roof then
        res.roof = build.box({ name = name .. "_roof", prim = prim, parent = par,
            pos = vec3(c.x, c.y + h + t * 0.5, c.z), size = vec3(sx + 2 * t, t, sz + 2 * t) })
    end

    -- One wall side as up to two segments leaving a centred doorway gap (width w, 0 = solid).
    local wi = 0
    local function side(sidename, cx, cz, axis, span)
        local w = (door and door.side == sidename and door.width) or 0.0
        local function seg(off, length)
            if length <= 1e-3 then return end
            wi = wi + 1
            local pos = (axis == "x") and vec3(cx + off, c.y + h * 0.5, cz)
                or vec3(cx, c.y + h * 0.5, cz + off)
            local size = (axis == "x") and vec3(length, h, t) or vec3(t, h, length)
            res.walls[wi] = build.box({ name = name .. "_wall_" .. wi, prim = prim, parent = par, pos = pos, size = size })
        end
        if w <= 0.0 then
            seg(0.0, span)
        else
            local part = (span - w) * 0.5
            seg(-(w * 0.5 + part * 0.5), part)
            seg(w * 0.5 + part * 0.5, part)
        end
    end

    side("-z", c.x, c.z - sz * 0.5 - t * 0.5, "x", sx)
    side("+z", c.x, c.z + sz * 0.5 + t * 0.5, "x", sx)
    side("-x", c.x - sx * 0.5 - t * 0.5, c.z, "z", sz)
    side("+x", c.x + sx * 0.5 + t * 0.5, c.z, "z", sz)
    return res
end

-- A solid ascending staircase from `base`: `steps` blocks, each `rise` taller and `run` deeper
-- along `dir` (XZ, default +Z), `width` wide. Step i is a block from the ground to its tread
-- height, so the result is a walkable ramp. opts: { base=vec3, steps=8, rise=0.25, run=0.3,
-- width=2, dir=vec3(0,0,1), prim, name, parent } -> array of handles.
function build.stairs(opts)
    opts = opts or {}
    local base = v3(opts.base)
    local steps = math.max(1, opts.steps or 8)
    local rise = opts.rise or 0.25
    local run = opts.run or 0.3
    local width = opts.width or 2.0
    local d = v3(opts.dir, 0.0, 0.0, 1.0)
    local dl = math.sqrt(d.x * d.x + d.z * d.z)
    if dl < 1e-6 then d, dl = vec3(0, 0, 1), 1.0 end
    local ux, uz = d.x / dl, d.z / dl
    local yaw = math.deg(math.atan(ux, uz))
    local out = {}
    for i = 0, steps - 1 do
        local h = (i + 1) * rise
        local off = (i + 0.5) * run
        out[i + 1] = build.box({
            name = opts.name and (opts.name .. "_" .. (i + 1)) or nil,
            prim = opts.prim, parent = opts.parent,
            pos = vec3(base.x + ux * off, base.y + h * 0.5, base.z + uz * off),
            size = vec3(width, h, run),
            rot = vec3(0.0, yaw, 0.0),
        })
    end
    return out
end

-- A fence along `from`..`to`: vertical posts every `spacing` units plus a top rail. opts:
--   { from, to, height=1.2, post=0.1, spacing=2.0, rail=0.08, prim, name, parent } -> handles.
function build.fence(opts)
    opts = opts or {}
    local a, b = v3(opts.from), v3(opts.to)
    local dx, dz = b.x - a.x, b.z - a.z
    local len = math.sqrt(dx * dx + dz * dz)
    if len < 1e-4 then return {} end
    local h = opts.height or 1.2
    local post = opts.post or 0.1
    local spacing = opts.spacing or 2.0
    local railT = opts.rail or 0.08
    local out = {}
    local count = math.max(1, math.floor(len / spacing))
    for i = 0, count do
        local f = i / count
        out[#out + 1] = build.box({
            name = opts.name and (opts.name .. "_post_" .. (i + 1)) or nil,
            prim = opts.prim, parent = opts.parent,
            pos = vec3(a.x + dx * f, a.y + h * 0.5, a.z + dz * f),
            size = vec3(post, h, post),
        })
    end
    out[#out + 1] = build.box({
        name = opts.name and (opts.name .. "_rail") or nil,
        prim = opts.prim, parent = opts.parent,
        pos = vec3((a.x + b.x) * 0.5, a.y + h - railT, (a.z + b.z) * 0.5),
        size = vec3(railT, railT, len),
        rot = vec3(0.0, math.deg(math.atan(dx, dz)), 0.0),
    })
    return out
end

-- A multi-storey building: an outer shell (build.room with roof + doorway) plus interior floor
-- slabs dividing the height into `floors` storeys. opts: { center, size=vec3(8,9,8), floors=1,
-- thickness=0.2, floor=0.2, doorway, prim, name, parent } -> { floor, roof, walls={}, slabs={} }.
function build.building(opts)
    opts = opts or {}
    local c = v3(opts.center)
    local s = v3(opts.size, 8.0, 9.0, 8.0)
    local floors = math.max(1, opts.floors or 1)
    local t = opts.thickness or 0.2
    local name = opts.name or "bld"
    local res = build.room({
        center = c, size = s, thickness = t, floor = opts.floor, roof = true,
        prim = opts.prim, name = name, doorway = opts.doorway, parent = opts.parent,
    })
    res.slabs = {}
    local storyH = s.y / floors
    for i = 1, floors - 1 do
        res.slabs[i] = build.box({
            name = name .. "_slab_" .. i, prim = opts.prim, parent = opts.parent,
            pos = vec3(c.x, c.y + i * storyH, c.z), size = vec3(s.x, t, s.z),
        })
    end
    return res
end

-- Scatter `count` primitives at random XZ positions within a rectangle, on the y plane.
-- Deterministic given `seed` (own LCG, no engine RNG). opts: { center=vec3, extent=vec3(5,0,5),
-- count=10, y=center.y, prim, size, name, parent, seed=1 } -> array of handles.
function build.scatter(opts)
    opts = opts or {}
    local c = v3(opts.center)
    local e = v3(opts.extent, 5.0, 0.0, 5.0)
    local n = math.max(0, opts.count or 10)
    local y = opts.y or c.y
    local state = ((opts.seed or 1) * 2654435761) % 2147483647
    local function rnd()
        state = (state * 1103515245 + 12345) % 2147483648
        return state / 2147483648.0
    end
    local out = {}
    for i = 1, n do
        out[i] = build.box({
            name = opts.name and (opts.name .. "_" .. i) or nil,
            prim = opts.prim, size = opts.size, parent = opts.parent,
            pos = vec3(c.x + (rnd() * 2.0 - 1.0) * e.x, y, c.z + (rnd() * 2.0 - 1.0) * e.z),
        })
    end
    return out
end

return build
