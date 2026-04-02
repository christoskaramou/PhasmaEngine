-- material_tweaker.lua
-- Attach to any node that has a mesh.
-- Exposed properties appear in the Properties panel and are applied live.

local props = exposed {
    -- Transmission / IOR
    transmission    = 1.0,
    ior             = 1.5,
    thickness       = 0.005,
    atten_distance  = 1e9,

    -- PBR
    metallic        = 0.0,
    roughness       = 0.5,

    -- Base color
    base_color_r    = 1.0,
    base_color_g    = 1.0,
    base_color_b    = 1.0,
    base_color_a    = 1.0,
}

local function apply()
    if not self then return end
    local info = self:get_mesh_info()
    if not info then return end

    material.set(self, "transmission",        props.transmission)
    material.set(self, "ior",                 props.ior)
    material.set(self, "thickness_factor",    props.thickness)
    material.set(self, "attenuation_distance", props.atten_distance)
    material.set(self, "metallic",            props.metallic)
    material.set(self, "roughness",           props.roughness)
    material.set(self, "base_color",
        vec4(props.base_color_r, props.base_color_g,
             props.base_color_b, props.base_color_a))
end

local function init()
    -- Seed props from the current material so the panel shows real values
    if not self then return end
    local info = self:get_mesh_info()
    if not info then return end

    local mat = material.get(self)
    if not mat then return end

    props.transmission   = mat.transmission   or props.transmission
    props.ior            = mat.ior            or props.ior
    props.thickness      = mat.thickness_factor or props.thickness
    props.atten_distance = mat.attenuation_distance or props.atten_distance
    props.metallic       = mat.metallic       or props.metallic
    props.roughness      = mat.roughness      or props.roughness
    if mat.base_color then
        props.base_color_r = mat.base_color.x or 1.0
        props.base_color_g = mat.base_color.y or 1.0
        props.base_color_b = mat.base_color.z or 1.0
        props.base_color_a = mat.base_color.w or 1.0
    end
end

hooks {
    -- init           = init,
    -- update_editor  = apply,
}
