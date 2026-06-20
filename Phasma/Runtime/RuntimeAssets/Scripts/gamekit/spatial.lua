-- gamekit/spatial.lua — declarative placement helpers over scene.digest().
--
-- The engine exposes the spatial facts (each node's world AABB, the aggregate
-- world_bounds, ground_y and pairwise overlaps) through scene.digest(); this module
-- turns those facts into intent: "sit this on the ground", "ring these around a
-- point", "push apart anything that overlaps". It is the precise-placement half of
-- the spatial-awareness track (the annotated map-shot render pass is the other),
-- so authoring scripts stop guessing coordinates and screenshotting to check.
--
-- COORDINATE NOTE: helpers move nodes via node:set_world_position, computing the
-- shift entirely in world space (the digest's AABBs are world-space too). The engine
-- maps the world target back through the parent's world matrix, so placement is exact
-- under ANY parent transform — rotated/scaled parents included, not just scene-root.

local spatial = {}

local function node_aabb(node)
    if not (node and node.get_bounding_box) then return nil end
    return node:get_bounding_box()
end

-- Move `node` so its world-AABB center lands on `target` (vec3). See COORDINATE NOTE.
-- bb.center and the node's world position are both world-space, so their difference is
-- the geometry's world offset; shifting world position by (target - center) is exact.
local function move_center_to(node, target)
    local bb = node_aabb(node)
    if not bb then return false end
    local c, w = bb.center, node:get_world_position()
    node:set_world_position(vec3(w.x + (target.x - c.x), w.y + (target.y - c.y), w.z + (target.z - c.z)))
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
    local w = node:get_world_position()
    node:set_world_position(vec3(w.x, w.y + (ground_y - bb.min.y), w.z))
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
--
-- Digests ONCE, not once per pass. scene.digest() reads the engine's CACHED world
-- AABBs, and set_world_position only marks a node dirty — the cache is not refreshed
-- until the next frame's matrix update. Re-digesting each iteration would therefore
-- read STALE centres and re-nudge an already-separated pair every pass (compounding
-- the push 4x). Instead we snapshot the pair list + XZ half-extents + the constant
-- origin→AABB-centre offset once, then drive every pass off the FRESH
-- get_world_position (offset and half-extents are invariant under the pure
-- translation we apply, so a live centre = get_world_position + offset).
--
-- The push is inverse-size weighted (a large object barely moves, the smaller node takes
-- most of the nudge), and a pair where one footprint dwarfs the other (ratio >
-- max_size_ratio) is skipped: its separation distance is the big node's half-extent, so a
-- nudge would eject the small node clear off it — you cannot slide a prop off the
-- ground/terrain it sits in. Those are left for the author.
-- opts: { iterations=4, padding=0.1, max_size_ratio=8 }
function spatial.resolve_overlaps(opts)
    opts = opts or {}
    local iterations = opts.iterations or 4
    local padding = opts.padding or 0.1
    local max_ratio = opts.max_size_ratio or 8.0
    if not (scene and scene.digest and scene.find_model) then return 0 end

    local d = scene.digest()
    if not d or not d.overlaps or #d.overlaps == 0 then return 0 end

    local pair_list = {}
    for _, ov in ipairs(d.overlaps) do
        local na, nb = d.nodes[ov.a], d.nodes[ov.b]
        local node_a = na and scene.find_model(na.name)
        local node_b = nb and scene.find_model(nb.name)
        if node_a and node_b then
            local hax, haz = na.aabb.size.x * 0.5, na.aabb.size.z * 0.5
            local hbx, hbz = nb.aabb.size.x * 0.5, nb.aabb.size.z * 0.5
            local sa, sb = math.max(hax, haz), math.max(hbx, hbz) -- footprint size of each
            local lo, hi = math.min(sa, sb), math.max(sa, sb)
            -- Skip world/terrain-scale mismatches (see header): a nudge would eject the
            -- small node off the big one rather than resolve a real prop-vs-prop overlap.
            if lo > 1e-4 and hi / lo <= max_ratio then
                local wa, wb = node_a:get_world_position(), node_b:get_world_position()
                pair_list[#pair_list + 1] = {
                    a = node_a, b = node_b,
                    hax = hax, haz = haz, hbx = hbx, hbz = hbz, sa = sa, sb = sb,
                    oax = na.aabb.center.x - wa.x, oaz = na.aabb.center.z - wa.z,
                    obx = nb.aabb.center.x - wb.x, obz = nb.aabb.center.z - wb.z,
                }
            end
        end
    end

    local moved = 0
    for _ = 1, iterations do
        moved = 0
        for _, p in ipairs(pair_list) do
            local wa, wb = p.a:get_world_position(), p.b:get_world_position()
            -- live centres from fresh world position + the constant origin→centre offset
            local dx = (wb.x + p.obx) - (wa.x + p.oax)
            local dz = (wb.z + p.obz) - (wa.z + p.oaz)
            local len = math.sqrt(dx * dx + dz * dz)
            if len < 1e-4 then dx, dz, len = 1.0, 0.0, 1.0 end -- coincident: split along +X
            local ux, uz = dx / len, dz / len
            local ra = math.abs(ux) * p.hax + math.abs(uz) * p.haz
            local rb = math.abs(ux) * p.hbx + math.abs(uz) * p.hbz
            local needed = ra + rb + padding
            if len < needed then
                local total = needed - len
                -- inverse-size weighting: larger footprint anchors, smaller node moves more
                local denom = p.sa + p.sb
                local move_a = (denom > 1e-4) and (total * p.sb / denom) or (total * 0.5)
                local move_b = total - move_a
                p.a:set_world_position(vec3(wa.x - ux * move_a, wa.y, wa.z - uz * move_a))
                p.b:set_world_position(vec3(wb.x + ux * move_b, wb.y, wb.z + uz * move_b))
                moved = moved + 1
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

-- ── Eye-level perception (over scene.decode_view / scene.pick_view_pixel) ──────────────────────
-- The engine's decode/pick bindings re-rasterize an object-id pass from the ACTIVE camera, so
-- these answer "what does the camera actually SEE" with exact occlusion — the perspective
-- complement to the top-down digest/map (which a roof or wall hides interiors from). They run a
-- one-off GPU pass + readback, so use them as authoring/verification calls, not per-frame logic.

-- Pass-through: occlusion-exact decode of the active camera view. Returns the engine table
-- { width, height, nodes = { { node, name, visible_pixels, distance, nearest_ndc_depth,
-- screen_box = { min_x, min_y, max_x, max_y } }, ... } } sorted by visible_pixels desc, or nil.
function spatial.decode_view(min_pixels)
    if not (scene and scene.decode_view) then return nil end
    return scene.decode_view(min_pixels)
end

-- Frame the active camera on `aabb` (default scene world_bounds) from `dir`, then decode what it
-- sees. opts = frame_camera opts { aabb, dir, padding } plus optional min_pixels. One call for
-- "point the camera at this area and tell me what's actually visible there", e.g. peeking a
-- camera through a doorway to perceive an interior the top-down map cannot.
function spatial.frame_and_decode(opts)
    opts = opts or {}
    if not spatial.frame_camera(opts) then return nil end
    return spatial.decode_view(opts.min_pixels)
end

-- Is `target` (a node handle OR a name string) actually visible to the active camera right now —
-- on-screen AND unoccluded? Returns { visible, pixels, coverage, distance, screen_box, name }:
--   coverage = visible pixels / total image pixels (a rough on-screen size, 0..1).
-- opts.frame = true first frames the camera on the node (opts.dir / opts.padding forwarded) so
-- "can the camera see this from a sensible angle?" is a single call. opts.min_pixels (default 1)
-- gates noise; opts.min_coverage (default 0) lets callers reject a barely-visible sliver.
--
-- Matches the decoded entry by name, so (like resolve_overlaps) it assumes the node's name is
-- unique; with duplicate names the first matching visible entry wins.
function spatial.verify_visible(target, opts)
    opts = opts or {}
    local result = { visible = false, pixels = 0, coverage = 0.0 }

    local node = target
    if type(target) == "string" then
        node = scene and scene.find_model and scene.find_model(target)
    end
    local name = (type(target) == "string") and target
        or (node and node.get_name and node:get_name()) or nil
    result.name = name
    if not name then return result end

    if opts.frame and node then
        spatial.frame_camera({
            aabb = node.get_bounding_box and node:get_bounding_box() or nil,
            dir = opts.dir,
            padding = opts.padding or 1.4,
        })
    end

    local view = spatial.decode_view(opts.min_pixels or 1)
    if not (view and view.nodes) then return result end

    local total = (view.width or 1) * (view.height or 1)
    for _, e in ipairs(view.nodes) do
        if e.name == name then
            result.pixels = e.visible_pixels or 0
            result.coverage = (total > 0) and (result.pixels / total) or 0.0
            result.distance = e.distance
            result.screen_box = e.screen_box
            result.visible = result.pixels >= (opts.min_pixels or 1)
                and result.coverage >= (opts.min_coverage or 0.0)
            break
        end
    end
    return result
end

-- ── Loop-closers: one call that decides AND acts ──────────────────────────────────────────────
-- verify_visible answers "is X visible from where the camera happens to point". These two close
-- the authoring loop the agent otherwise drives by hand (aim -> decode -> read -> re-aim): they
-- SEARCH for a working camera angle and REPORT what blocks the shot, in a single execute_lua call.
--
-- FRESHLY-BUILT NODES: decode re-rasterizes the EXISTING GPU-culled indirect draws, so a node
-- created THIS call is not in the draw set yet and reads as 0 pixels (false "invisible"). Build in
-- one call, then verify in a LATER call (one rendered frame is enough); moving an already-drawn
-- node is fine. The scene digest has no such lag — it reads CPU-side AABBs immediately.

-- Do two screen-space boxes (each { min_x, min_y, max_x, max_y }) overlap?
local function boxes_overlap(a, b)
    return a and b
        and a.min_x <= b.max_x and a.max_x >= b.min_x
        and a.min_y <= b.max_y and a.max_y >= b.min_y
end

-- Decoded nodes that sit IN FRONT of a target's screen region — the likely occluders. A node
-- qualifies when its screen_box overlaps `screen_box` and it is nearer than `distance`. Sorted by
-- visible pixels (biggest blocker first). Returns {} when screen_box is nil (target fully hidden,
-- so decode never reported its box) — find_view_of has already exhausted the angles by then.
-- Pass `view` to reuse an existing decode_view result instead of issuing another GPU pass.
function spatial._occluders_of(screen_box, distance, view)
    local occ = {}
    if not screen_box then return occ end
    view = view or spatial.decode_view(1)
    if not (view and view.nodes) then return occ end
    local td = distance or math.huge
    for _, e in ipairs(view.nodes) do
        if e.distance and e.distance < td and boxes_overlap(e.screen_box, screen_box) then
            occ[#occ + 1] = { name = e.name, distance = e.distance, pixels = e.visible_pixels }
        end
    end
    table.sort(occ, function(x, y) return (x.pixels or 0) > (y.pixels or 0) end)
    return occ
end

-- Candidate look-directions (camera forward vectors) to try when hunting for a clear view.
-- opts: { dir (force a single direction), azimuths=6, pitch_deg=35, azimuth_offset=0,
--         include_current=true, include_top=true }. Each azimuth orbits the target on the XZ
-- plane at a fixed downward pitch, so the camera ends up above and to the side looking in — one
-- of them peeks through a doorway/window the straight-down view cannot.
local function candidate_dirs(opts)
    if opts.dir then return { opts.dir } end
    local dirs = {}
    if opts.include_current ~= false then
        local cam = get_camera and get_camera()
        if cam and cam.get_front then dirs[#dirs + 1] = cam:get_front() end
    end
    local pitch = math.rad(opts.pitch_deg or 35.0)
    local cp, sp = math.cos(pitch), math.sin(pitch)
    local n = math.max(1, opts.azimuths or 6)
    local off = math.rad(opts.azimuth_offset or 0)
    for i = 0, n - 1 do
        local a = off + (i / n) * 2 * math.pi
        dirs[#dirs + 1] = vec3(cp * math.sin(a), -sp, cp * math.cos(a))
    end
    -- Slight tilt avoids the degenerate up-vector of a dead-vertical look_at.
    if opts.include_top ~= false then dirs[#dirs + 1] = vec3(0.05, -1.0, 0.05) end
    return dirs
end

-- Find a camera angle that actually SEES `target` (node handle or name) and leave the camera
-- there. Frames the target from each candidate direction, decodes, and keeps the one with the most
-- visible coverage — so it routes around occluders instead of trusting a single guessed angle.
-- opts: candidate_dirs opts (dir/azimuths/pitch_deg/...) + { aabb (override target box),
--   padding=1.4, min_pixels=1, min_coverage=0 (found requires >= this), good_enough=0.04
--   (stop early once a candidate clears this coverage) }.
-- Returns { found, name, coverage, pixels, distance, screen_box, dir, candidates = {
--   { dir, coverage, pixels }, ... } }. `found=false` with a non-empty candidates list means it
--   was tried from every angle and stayed hidden/too small.
function spatial.find_view_of(target, opts)
    opts = opts or {}
    local node = target
    if type(target) == "string" then
        node = scene and scene.find_model and scene.find_model(target)
    end
    local name = (type(target) == "string") and target
        or (node and node.get_name and node:get_name()) or nil
    local box = opts.aabb or (node and node.get_bounding_box and node:get_bounding_box()) or nil
    local out = { found = false, name = name, coverage = 0.0, pixels = 0, candidates = {} }
    if not (name and box) then return out end

    local padding = opts.padding or 1.4
    local good_enough = opts.good_enough or 0.04
    local best
    for _, dir in ipairs(candidate_dirs(opts)) do
        if spatial.frame_camera({ aabb = box, dir = dir, padding = padding }) then
            local v = spatial.verify_visible(name, { min_pixels = opts.min_pixels or 1 })
            out.candidates[#out.candidates + 1] = { dir = dir, coverage = v.coverage, pixels = v.pixels }
            if v.visible and (not best or v.coverage > best.coverage) then
                best = { dir = dir, coverage = v.coverage, pixels = v.pixels,
                    distance = v.distance, screen_box = v.screen_box }
                if v.coverage >= good_enough then break end -- clear enough; stop searching
            end
        end
    end

    if best then
        -- Re-apply the winning pose (the loop may have ended on a worse candidate).
        spatial.frame_camera({ aabb = box, dir = best.dir, padding = padding })
        out.coverage, out.pixels = best.coverage, best.pixels
        out.distance, out.screen_box, out.dir = best.distance, best.screen_box, best.dir
        out.found = best.coverage >= (opts.min_coverage or 0.0)
    end
    return out
end

-- Place `node` (handle or name) and confirm it can be seen — the build/edit loop in one call.
-- Moves the node's AABB center to `pos` (vec3; nil = leave where it is), optionally settles it onto
-- the ground and pushes it out of overlaps, then finds a viewing angle and reports the result.
-- opts: { on_ground=false, ground_y, resolve=false (true or a resolve_overlaps opts table),
--   find_view=true (false = just verify from the current camera, don't move it),
--   plus all find_view_of opts (padding/azimuths/min_coverage/...) }.
-- Returns { node, name, placed, visible, coverage, pixels, distance, screen_box, occluders }.
-- When the node ends up not (well) visible, `occluders` lists what stands in front of it.
function spatial.place_and_verify(node, pos, opts)
    opts = opts or {}
    if type(node) == "string" then
        node = scene and scene.find_model and scene.find_model(node)
    end
    local out = { node = node, placed = false, visible = false, coverage = 0.0, pixels = 0, occluders = {} }
    if not node then return out end
    out.name = node.get_name and node:get_name() or nil

    if pos then out.placed = move_center_to(node, pos) else out.placed = true end
    if opts.on_ground then spatial.place_on_ground(node, opts.ground_y) end
    if opts.resolve then
        spatial.resolve_overlaps(type(opts.resolve) == "table" and opts.resolve or {})
    end

    if opts.find_view ~= false then
        local v = spatial.find_view_of(node, opts)
        out.coverage, out.pixels = v.coverage, v.pixels
        out.distance, out.screen_box, out.visible = v.distance, v.screen_box, v.found
    else
        local v = spatial.verify_visible(node, {
            min_pixels = opts.min_pixels or 1,
            min_coverage = opts.min_coverage or 0.0,
        })
        out.coverage, out.pixels = v.coverage, v.pixels
        out.distance, out.screen_box, out.visible = v.distance, v.screen_box, v.visible
    end

    if (not out.visible) or out.coverage < (opts.min_coverage or 0.0) then
        out.occluders = spatial._occluders_of(out.screen_box, out.distance)
    end
    return out
end

return spatial
