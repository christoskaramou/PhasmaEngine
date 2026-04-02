-- Material binding test suite

function run_material_tests()
    pe_log("=== Material Tests ===")
    T.reset()

    -- Create a primitive to test materials on
    -- primitives.cube() returns a SceneNodeHandle
    local cube = primitives.cube(1.0)
    T.check("cube created", cube ~= nil)

    if not cube then
        T.summary("Material Tests")
        return
    end

    -- get material for the node (primitive nodes have a mesh attached)
    local mat = material.get(cube)
    T.check("material.get returns table", mat ~= nil)

    if mat then
        -- Check default values (from Primitives.cpp)
        T.check("default metallic", mat.metallic == 0.0)
        T.check("default roughness", mat.roughness == 1.0)
        T.check("default alpha_cutoff", mat.alpha_cutoff == 0.5)
        T.check("default occlusion_strength", mat.occlusion_strength == 1.0)
        T.check("default normal_scale", mat.normal_scale == 1.0)
        T.check("default transmission", mat.transmission == 0.0)
    end

    -- set and verify metallic
    material.set(cube, "metallic", 0.25)
    local mat2 = material.get(cube)
    T.check("set metallic", mat2 and mat2.metallic == 0.25)

    -- set and verify roughness
    material.set(cube, "roughness", 0.5)
    local mat3 = material.get(cube)
    T.check("set roughness", mat3 and mat3.roughness == 0.5)

    -- set and verify base_color
    material.set(cube, "base_color", vec4(1.0, 0.0, 0.0, 1.0))
    local mat4r = material.get(cube)
    T.check("set base_color red", mat4r and mat4r.base_color.x == 1.0 and mat4r.base_color.y == 0.0)

    -- set and verify emissive
    material.set(cube, "emissive", vec3(0.5, 0.25, 0.125))
    local mat5 = material.get(cube)
    T.check("set emissive", mat5 and mat5.emissive.x == 0.5 and mat5.emissive.y == 0.25)

    -- get_render_type
    local rt = material.get_render_type(cube)
    T.check("get_render_type", rt == "opaque" or rt == "alpha_cut" or rt == "alpha_blend" or rt == "transmission")

    -- get_texture_mask (primitives have no textures)
    local mask = material.get_texture_mask(cube)
    T.check("get_texture_mask", mask == 0)

    -- has_texture
    T.check("has_texture base_color false", material.has_texture(cube, "base_color") == false)

    -- set_texture with non-existent file (should return false, not crash)
    local tex_ok = material.set_texture(cube, "base_color", "nonexistent_texture.png")
    T.check("set_texture missing file returns false", tex_ok == false)

    -- invalid handle (default-constructed) should return nil gracefully
    -- We cannot easily construct an invalid SceneNode from Lua, so skip this check.

    -- cleanup
    cube:remove()

    T.summary("Material Tests")
end
