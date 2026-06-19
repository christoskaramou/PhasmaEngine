-- gamekit/scene.lua — thin, convention-friendly lookups over the engine scene API.
--
-- The engine only exposes find_model(name) (exact name). gamekit leans on a naming
-- convention for pools and groups: members are suffixed with a zero-padded index,
-- e.g. Enemy_001 .. Enemy_032. find_all(prefix) walks that convention so callers
-- never hard-code member counts.

local kscene = {}

-- Find a single node by exact name. Returns a node handle, or nil.
function kscene.find(name)
    if not scene or not scene.find_model then return nil end
    return scene.find_model(name)
end

-- Format a pool member name from a prefix and 1-based index ("Enemy" -> "Enemy_001").
function kscene.member_name(prefix, index)
    return string.format("%s_%03d", prefix, index)
end

-- Collect every node named "<prefix>_NNN" (1-based, zero-padded to 3). Scans
-- indices 1..max and gathers the ones that exist, tolerating gaps. Returns a list
-- of node handles in index order.
function kscene.find_all(prefix, max)
    local out = {}
    if not scene or not scene.find_model then return out end
    max = max or 256
    for i = 1, max do
        local node = scene.find_model(kscene.member_name(prefix, i))
        if node ~= nil then
            out[#out + 1] = node
        end
    end
    return out
end

-- The active camera object (with get/set_position, look_at, frustum helpers, ...).
function kscene.active_camera()
    if get_camera then return get_camera() end
    if scene and scene.get_active_camera then return scene.get_active_camera() end
    return nil
end

return kscene
