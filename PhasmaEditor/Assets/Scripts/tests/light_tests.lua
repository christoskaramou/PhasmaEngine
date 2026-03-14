-- Light binding test suite

function run_light_tests()
    pe_log("=== Light Tests ===")
    T.reset()

    -- get_counts
    local counts = lights.get_counts()
    T.check("get_counts returns table", counts ~= nil)
    T.check("get_counts has point", counts.point ~= nil)
    T.check("get_counts has directional", counts.directional ~= nil)
    T.check("get_counts has spot", counts.spot ~= nil)
    T.check("get_counts has area", counts.area ~= nil)

    -- Point lights
    local initial_point = counts.point
    lights.add_point()
    local counts2 = lights.get_counts()
    T.check("add_point increases count", counts2.point == initial_point + 1)

    local pls = lights.get_point_lights()
    T.check("get_point_lights not empty", #pls > 0)

    local last = #pls
    local pl = pls[last]
    T.check("point light has name", pl.name ~= nil)
    T.check("point light has color", pl.color ~= nil)
    T.check("point light has intensity", pl.intensity ~= nil)
    T.check("point light has position", pl.position ~= nil)
    T.check("point light has radius", pl.radius ~= nil)

    -- set_point_light
    lights.set_point_light(pl.index, vec3(1, 2, 4), vec3(1, 0, 0), 2.0, 8.0)
    local pls2 = lights.get_point_lights()
    local updated = pls2[last]
    T.check("set_point_light position", updated.position.x == 1.0 and updated.position.y == 2.0 and updated.position.z == 4.0)
    T.check("set_point_light color", updated.color.x == 1.0 and updated.color.y == 0.0)
    T.check("set_point_light intensity", updated.intensity == 2.0)
    T.check("set_point_light radius", updated.radius == 8.0)

    lights.remove_point(pl.index)
    local counts3 = lights.get_counts()
    T.check("remove_point decreases count", counts3.point == initial_point)

    -- Spot lights
    local initial_spot = counts.spot
    lights.add_spot()
    local counts4 = lights.get_counts()
    T.check("add_spot increases count", counts4.spot == initial_spot + 1)

    local sls = lights.get_spot_lights()
    T.check("get_spot_lights not empty", #sls > 0)

    local sl = sls[#sls]
    T.check("spot light has angle", sl.angle ~= nil)
    T.check("spot light has falloff", sl.falloff ~= nil)
    T.check("spot light has range", sl.range ~= nil)

    lights.set_spot_light(sl.index, vec3(0, 4, 0), vec3(0, 1, 0), 4.0, 16.0, 0.5, 0.25)
    local sls2 = lights.get_spot_lights()
    local sl2 = sls2[#sls2]
    T.check("set_spot_light intensity", sl2.intensity == 4.0)
    T.check("set_spot_light angle", sl2.angle == 0.5)
    T.check("set_spot_light falloff", sl2.falloff == 0.25)

    lights.remove_spot(sl.index)
    local counts5 = lights.get_counts()
    T.check("remove_spot restores count", counts5.spot == initial_spot)

    -- Area lights
    local initial_area = counts.area
    lights.add_area()
    local counts6 = lights.get_counts()
    T.check("add_area increases count", counts6.area == initial_area + 1)

    local als = lights.get_area_lights()
    T.check("get_area_lights not empty", #als > 0)

    local al = als[#als]
    T.check("area light has width", al.width ~= nil)
    T.check("area light has height", al.height ~= nil)

    lights.set_area_light(al.index, vec3(0, 0, 0), vec3(1, 1, 1), 2.0, 8.0, 4.0, 2.0)
    local als2 = lights.get_area_lights()
    local al2 = als2[#als2]
    T.check("set_area_light width", al2.width == 4.0)
    T.check("set_area_light height", al2.height == 2.0)

    lights.remove_area(al.index)
    local counts7 = lights.get_counts()
    T.check("remove_area restores count", counts7.area == initial_area)

    -- Directional lights
    local dls = lights.get_directional_lights()
    if #dls > 0 then
        local dl = dls[1]
        lights.set_directional_light(dl.index, vec3(0, 1, 0), vec3(1, 1, 0.5), 0.5)
        local dls2 = lights.get_directional_lights()
        T.check("set_directional_light color", dls2[1].color.x == 1.0 and dls2[1].color.z == 0.5)
        T.check("set_directional_light intensity", dls2[1].intensity == 0.5)
    end

    T.summary("Light Tests")
end
