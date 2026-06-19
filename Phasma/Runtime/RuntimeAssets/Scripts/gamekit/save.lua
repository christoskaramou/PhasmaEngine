-- gamekit/save.lua — persist run state as JSON under Assets/Save/.
--
-- write(slot, tbl) serialises a Lua table with gamekit/json and writes it via the
-- fs binding (sandboxed to the project Assets). read(slot) parses it back, or nil
-- if missing/corrupt. The json dependency is injected by gamekit/init (save._json).

local save = {
    _json = nil,
    dir = "Save/",
}

local function path_for(slot)
    return save.dir .. tostring(slot) .. ".json"
end

function save.write(slot, tbl)
    if not (fs and fs.write and save._json) then return false end
    local ok = fs.write(path_for(slot), save._json.encode(tbl or {}))
    return ok ~= false
end

function save.read(slot)
    if not (fs and fs.read and save._json) then return nil end
    local text = fs.read(path_for(slot))
    if not text then return nil end
    local value, err = save._json.decode(text)
    if err then return nil, err end
    return value
end

function save.exists(slot)
    if not (fs and fs.read) then return false end
    return fs.read(path_for(slot)) ~= nil
end

return save
