-- gamekit/json.lua — JSON encode/decode via engine json_encode/json_decode bindings.

local json = {}

function json.encode(value)
    return json_encode(value)
end

function json.decode(text)
    local result = json_decode(text or "")
    if result.error then
        return nil, result.error
    end
    return result.value
end

return json
