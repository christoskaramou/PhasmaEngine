local props = exposed {
    spin = 38.0,
    bob = 0.22,
    drift = 0.10
}

local base = nil
local phase = 0.0

hooks {
    init = function()
        base = transform:get_position()
        phase = base.x * 0.173 + base.z * 0.097
    end,

    update_editor = function()
        if not base then base = transform:get_position() end
        local t = os.clock() + phase
        local p = vec3(
            base.x + math.sin(t * 0.70) * props.drift,
            base.y + math.sin(t * 1.80) * props.bob,
            base.z + math.cos(t * 0.65) * props.drift)
        transform:set_position(p)
        transform:set_rotation(vec3(t * props.spin, t * props.spin * 0.71, t * props.spin * 1.23))
    end,
}
