-- gamekit/spatial.lua — declarative placement helpers over scene.digest().
--
-- The engine exposes the spatial facts (each node's world AABB, the aggregate
-- world_bounds, ground_y and pairwise overlaps) through scene.digest(); this module
-- turns those facts into intent: "sit this on the ground", "ring these around a
-- point", "push apart anything that overlaps". It is the precise-placement half of
-- the spatial-awareness track (the annotated map-shot render pass is the other),
-- so authoring scripts stop guessing coordinates and screenshotting to check.
--
-- COORDINATE NOTE: helpers move nodes via node:set_position (LOCAL) using a world
-- delta. That is exact for scene-root nodes and any node whose parent chain is
-- unrotated/unscaled (the common authoring case). Under a rotated/scaled parent the
-- world delta is only approximate — parent the node to root, or set it directly.

local spatial = {}

local function node_aabb(node)
    if not (node and node.get_bounding_box) then return nil end
    return node:get_bounding_box()
end

-- Move `node` so its world-AABB center lands on `target` (vec3). See COORDINATE NOTE.
local function move_center_to(node, target)
    local bb = node_aabb(node)
    if not bb then return false end
    local c, p = bb.center, node:get_position()
    node:set_position(vec3(p.x + (target.x - c.x), p.y + (target.y - c.y), p.z + (target.z - c.z)))
    return true
end
spatial.move_center_to = move_center_to

-- Convenience reads off the digest.
function spatial.bounds()
    local d = scene and scene.digest and scene.digest()
    return d and d.world_bounds or nil
end

function spatial.ground_y()
    local d = scene and scene.digest and scene.digest()
    return (d and d.ground_y) or 0.0
end

-- Sit `node` on the ground: its AABB bottom (min.y) rests at `ground_y` (default =
-- the scene's digest ground_y), XZ left untouched.
function spatial.place_on_ground(node, ground_y)
    local bb = node_aabb(node)
    if not bb then return false end
    ground_y = ground_y or spatial.ground_y()
    local p = node:get_position()
    node:set_position(vec3(p.x, p.y + (ground_y - bb.min.y), p.z))
    return true
end

-- Place `node` `dist` units in front of the active camera along its forward vector.
-- If `on_ground`, then drop it to the ground plane keeping the camera-forward XZ.
function spatial.in_front_of_camera(node, dist, on_ground)
    dist = dist or 5.0
    local cam = get_camera and get_camera()
    if not cam then return false end
    local pos, front = cam:get_position(), cam:get_front()
    local ok = move_center_to(node, vec3(pos.x + front.x * dist, pos.y + front.y * dist, pos.z + front.z * dist))
    if ok and on_ground then spatial.place_on_ground(node) end
    return ok
end

-- Arrange `nodes` (array) evenly around a circle on the XZ plane.
-- opts: { center=vec3(0,0,0), radius=5, y=center.y, start_deg=0 }
function spatial.ring(nodes, opts)
    opts = opts or {}
    local center = opts.center or vec3(0, 0, 0)
    local radius = opts.radius or 5.0
    local y = opts.y or center.y
    local start = math.rad(opts.start_deg or 0)
    local n = #nodes
    if n == 0 then return end
    for i, node in ipairs(nodes) do
        local a = start + (i - 1) * (2 * math.pi / n)
        move_center_to(node, vec3(center.x + math.cos(a) * radius, y, center.z + math.sin(a) * radius))
    end
end

-- Arrange `nodes` in a row-major grid on the XZ plane.
-- opts: { origin=vec3(0,0,0), cols=ceil(sqrt(n)), spacing=2, spacing_z=spacing, y=origin.y }
function spatial.grid(nodes, opts)
    opts = opts or {}
    local origin = opts.origin or vec3(0, 0, 0)
    local n = #nodes
    if n == 0 then return end
    local cols = opts.cols or math.ceil(math.sqrt(n))
    local sx = opts.spacing or 2.0
    local sz = opts.spacing_z or sx
    local y = opts.y or origin.y
    for i, node in ipairs(nodes) do
        local k = i - 1
        move_center_to(node, vec3(origin.x + (k % cols) * sx, y, origin.z + math.floor(k / cols) * sz))
    end
end

-- Stack `nodes` along +Y so each rests on the one below, using their AABB heights.
-- opts: { base=vec3(0,0,0) — XZ column + floor height, gap=0 }
function spatial.stack(nodes, opts)
    opts = opts or {}
    local base = opts.base or vec3(0, 0, 0)
    local gap = opts.gap or 0.0
    local cursor = base.y
    for _, node in ipairs(nodes) do
        local bb = node_aabb(node)
        if bb then
            local h = bb.size.y
            move_center_to(node, vec3(base.x, cursor + h * 0.5, base.z))
            cursor = cursor + h + gap
        end
    end
end

-- Push apart any nodes whose AABBs overlap (per scene.digest()). Each overlapping
-- pair is separated horizontally along the line between their centers using a
-- separating-axis estimate of the XZ penetration; `iterations` passes converge
-- crowded clusters. Returns the count of pairs nudged on the final pass (0 = clean).
--
-- Re-acquires nodes from the digest by name, so it assumes authored nodes have
-- unique names (disabled pool members are excluded from digest overlaps anyway).
-- opts: { iterations=4, padding=0.1 }
function spatial.resolve_overlaps(opts)
    opts = opts or {}
    local iterations = opts.iterations or 4
    local padding = opts.padding or 0.1
    if not (scene and scene.digest and scene.find_model) then return 0 end
    local moved = 0
    for _ = 1, iterations do
        local d = scene.digest()
        if not d or not d.overlaps or #d.overlaps == 0 then
            moved = 0
            break
        end
        moved = 0
        for _, ov in ipairs(d.overlaps) do
            local na, nb = d.nodes[ov.a], d.nodes[ov.b]
            local node_a = na and scene.find_model(na.name)
            local node_b = nb and scene.find_model(nb.name)
            if node_a and node_b then
                local ca, cb = na.aabb.center, nb.aabb.center
                local dx, dz = cb.x - ca.x, cb.z - ca.z
                local len = math.sqrt(dx * dx + dz * dz)
                if len < 1e-4 then dx, dz, len = 1.0, 0.0, 1.0 end -- coincident: split along +X
                local ux, uz = dx / len, dz / len
                local ra = math.abs(ux) * na.aabb.size.x * 0.5 + math.abs(uz) * na.aabb.size.z * 0.5
                local rb = math.abs(ux) * nb.aabb.size.x * 0.5 + math.abs(uz) * nb.aabb.size.z * 0.5
                local needed = ra + rb + padding
                if len < needed then
                    local half = (needed - len) * 0.5
                    local pa, pb = node_a:get_position(), node_b:get_position()
                    node_a:set_position(vec3(pa.x - ux * half, pa.y, pa.z - uz * half))
                    node_b:set_position(vec3(pb.x + ux * half, pb.y, pb.z + uz * half))
                    moved = moved + 1
                end
            end
        end
        if moved == 0 then break end
    end
    return moved
end

-- Position the active camera to frame `aabb` (default = scene world_bounds). Sizes
-- an orthographic camera to fit and backs a perspective one off by its FOV, then
-- looks at the box center. opts: { aabb, dir=cam.front, padding=1.25 }.
--
-- Sets the active Camera object directly — ideal for an editor/free camera. A camera
-- bound to a scene node may be re-synced from its node transform next frame; move
-- that node (or use the MCP set_camera/frame_node tools) for node-bound cameras.
function spatial.frame_camera(opts)
    opts = opts or {}
    local cam = get_camera and get_camera()
    if not cam then return false end
    local box = opts.aabb or spatial.bounds()
    if not box then return false end

    local center, size = box.center, box.size
    local radius = math.max(size.x, size.y, size.z) * 0.5
    if radius <= 0 then radius = 1.0 end
    radius = radius * (opts.padding or 1.25)

    local dir = opts.dir or cam:get_front()
    local dl = math.sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z)
    if dl < 1e-6 then dir, dl = vec3(0, -1, 0), 1.0 end
    dir = vec3(dir.x / dl, dir.y / dl, dir.z / dl)

    local dist
    if cam.is_orthographic and cam:is_orthographic() then
        if cam.set_orthographic_size then cam:set_orthographic_size(radius) end
        dist = radius * 3.0
    else
        local fov_rad = math.rad((cam.get_fov and cam:get_fov()) or 60.0)
        dist = radius / math.tan(math.max(fov_rad, 0.1) * 0.5)
    end
    cam:set_position(vec3(center.x - dir.x * dist, center.y - dir.y * dist, center.z - dir.z * dist))
    if cam.look_at then cam:look_at(center) end
    return true
end

return spatial
