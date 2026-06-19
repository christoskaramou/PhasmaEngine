-- gamekit/json.lua — minimal, dependency-free JSON encode/decode.
--
-- PhasmaEngine's Lua opens no JSON library, so gamekit ships its own (mirrors the
-- hand-rolled encoder/decoder proven in AgainstTheHero). Used by gamekit/save.lua
-- to persist run state under Assets/Save/. encode() sorts object keys for stable
-- output; decode() returns (value) or (nil, error_message).

local json = {}

local function escape_string(value)
    value = tostring(value or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\"", "\\\"")
    value = value:gsub("\n", "\\n")
    value = value:gsub("\r", "\\r")
    value = value:gsub("\t", "\\t")
    return value
end

local function is_array(value)
    local count, max_index = 0, 0
    for key, _ in pairs(value) do
        if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then return false end
        count = count + 1
        if key > max_index then max_index = key end
    end
    return max_index == count
end

function json.encode(value)
    local kind = type(value)
    if kind == "nil" then return "null" end
    if kind == "boolean" then return value and "true" or "false" end
    if kind == "number" then return tostring(value) end
    if kind == "string" then return "\"" .. escape_string(value) .. "\"" end
    if kind ~= "table" then return "\"" .. escape_string(value) .. "\"" end

    local parts = {}
    if is_array(value) then
        for i = 1, #value do
            parts[#parts + 1] = json.encode(value[i])
        end
        return "[" .. table.concat(parts, ",") .. "]"
    end

    for key, entry in pairs(value) do
        parts[#parts + 1] = "\"" .. escape_string(key) .. "\":" .. json.encode(entry)
    end
    table.sort(parts)
    return "{" .. table.concat(parts, ",") .. "}"
end

function json.decode(text)
    text = tostring(text or "")
    local index, length = 1, #text

    local function fail(message)
        return nil, message .. " at byte " .. tostring(index)
    end
    local function peek() return text:sub(index, index) end
    local function skip_ws()
        while index <= length do
            local c = peek()
            if c ~= " " and c ~= "\n" and c ~= "\r" and c ~= "\t" then break end
            index = index + 1
        end
    end

    local parse_value

    local function parse_string()
        if peek() ~= "\"" then return fail("expected string") end
        index = index + 1
        local out = {}
        while index <= length do
            local c = peek()
            index = index + 1
            if c == "\"" then return table.concat(out) end
            if c == "\\" then
                local esc = peek()
                index = index + 1
                if esc == "\"" or esc == "\\" or esc == "/" then
                    out[#out + 1] = esc
                elseif esc == "b" then out[#out + 1] = "\b"
                elseif esc == "f" then out[#out + 1] = "\f"
                elseif esc == "n" then out[#out + 1] = "\n"
                elseif esc == "r" then out[#out + 1] = "\r"
                elseif esc == "t" then out[#out + 1] = "\t"
                elseif esc == "u" then
                    local hex = text:sub(index, index + 3)
                    index = index + 4
                    local cp = tonumber(hex, 16)
                    out[#out + 1] = cp and utf8 and utf8.char and utf8.char(cp) or "?"
                else
                    return fail("invalid string escape")
                end
            else
                out[#out + 1] = c
            end
        end
        return fail("unterminated string")
    end

    local function parse_number()
        local start = index
        if peek() == "-" then index = index + 1 end
        while index <= length and peek():match("%d") do index = index + 1 end
        if peek() == "." then
            index = index + 1
            while index <= length and peek():match("%d") do index = index + 1 end
        end
        local c = peek()
        if c == "e" or c == "E" then
            index = index + 1
            c = peek()
            if c == "+" or c == "-" then index = index + 1 end
            while index <= length and peek():match("%d") do index = index + 1 end
        end
        local value = tonumber(text:sub(start, index - 1))
        if value == nil then return fail("invalid number") end
        return value
    end

    local function parse_array()
        index = index + 1
        local out = {}
        skip_ws()
        if peek() == "]" then index = index + 1; return out end
        while index <= length do
            local value, err = parse_value()
            if err then return nil, err end
            out[#out + 1] = value
            skip_ws()
            local c = peek()
            if c == "]" then index = index + 1; return out end
            if c ~= "," then return fail("expected ',' or ']'") end
            index = index + 1
        end
        return fail("unterminated array")
    end

    local function parse_object()
        index = index + 1
        local out = {}
        skip_ws()
        if peek() == "}" then index = index + 1; return out end
        while index <= length do
            skip_ws()
            local key, key_err = parse_string()
            if key_err then return nil, key_err end
            skip_ws()
            if peek() ~= ":" then return fail("expected ':'") end
            index = index + 1
            local value, value_err = parse_value()
            if value_err then return nil, value_err end
            out[key] = value
            skip_ws()
            local c = peek()
            if c == "}" then index = index + 1; return out end
            if c ~= "," then return fail("expected ',' or '}'") end
            index = index + 1
        end
        return fail("unterminated object")
    end

    function parse_value()
        skip_ws()
        local c = peek()
        if c == "\"" then return parse_string() end
        if c == "{" then return parse_object() end
        if c == "[" then return parse_array() end
        if c == "-" or c:match("%d") then return parse_number() end
        if text:sub(index, index + 3) == "true" then index = index + 4; return true end
        if text:sub(index, index + 4) == "false" then index = index + 5; return false end
        if text:sub(index, index + 3) == "null" then index = index + 4; return nil end
        return fail("unexpected JSON value")
    end

    local value, err = parse_value()
    if err then return nil, err end
    skip_ws()
    if index <= length then return nil, "trailing JSON data at byte " .. tostring(index) end
    return value
end

return json
