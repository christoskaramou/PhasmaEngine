-- Particle binding test suite

function run_particle_tests()
    pe_log("=== Particle Tests ===")
    T.reset()

    -- get_count (initial)
    local initial = particles.get_count()
    T.check("get_count returns number", type(initial) == "number")

    -- add_emitter with defaults
    local idx = particles.add_emitter()
    T.check("add_emitter returns index", idx >= 0)
    T.check("count increased", particles.get_count() == initial + 1)

    -- get_emitter
    local e = particles.get_emitter(idx)
    T.check("get_emitter returns table", e ~= nil)
    if e then
        T.check("emitter has position", e.position ~= nil)
        T.check("emitter has velocity", e.velocity ~= nil)
        T.check("emitter has color_start", e.color_start ~= nil)
        T.check("emitter has count", e.count == 100)
        T.check("emitter has spawn_rate", type(e.spawn_rate) == "number")
        T.check("emitter has drag", type(e.drag) == "number")
        T.check("emitter has orientation", e.orientation == 0)
    end

    -- set_emitter properties
    particles.set_emitter(idx, "position", vec3(1, 2, 4))
    local e2 = particles.get_emitter(idx)
    T.check("set position", e2.position.x == 1.0 and e2.position.y == 2.0 and e2.position.z == 4.0)

    particles.set_emitter(idx, "count", 200)
    local e3 = particles.get_emitter(idx)
    T.check("set count", e3.count == 200)

    particles.set_emitter(idx, "spawn_rate", 128.0)
    local e4 = particles.get_emitter(idx)
    T.check("set spawn_rate", e4.spawn_rate == 128.0)

    particles.set_emitter(idx, "name", "test_emitter")
    local e5 = particles.get_emitter(idx)
    T.check("set name", e5.name == "test_emitter")

    -- add_emitter with options table
    local idx2 = particles.add_emitter({
        position = vec3(8, 0, 0),
        velocity = vec3(0, 2, 0),
        count = 50,
        size_min = 0.125,
        size_max = 0.25,
        orientation = "velocity"
    })
    T.check("add_emitter with opts", idx2 >= 0)

    local e6 = particles.get_emitter(idx2)
    T.check("opts position", e6.position.x == 8.0)
    T.check("opts count", e6.count == 50)
    T.check("opts size_min", e6.size_min == 0.125)
    T.check("opts orientation velocity", e6.orientation == 3)

    -- animation properties via add_emitter options
    local idx3 = particles.add_emitter({
        count = 10,
        texture_index = 0,
        anim_rows = 4,
        anim_cols = 4,
        anim_speed = 2.5,
        interpolate = true
    })
    T.check("add_emitter with animation opts", idx3 >= 0)

    local ea = particles.get_emitter(idx3)
    T.check("get anim_rows", ea.anim_rows == 4.0)
    T.check("get anim_cols", ea.anim_cols == 4.0)
    T.check("get anim_speed", ea.anim_speed == 2.5)
    T.check("get interpolate", ea.interpolate == true)
    T.check("get texture_index", ea.texture_index == 0)

    -- set_emitter animation properties
    particles.set_emitter(idx3, "anim_rows", 8)
    particles.set_emitter(idx3, "anim_speed", 5.0)
    particles.set_emitter(idx3, "interpolate", false)
    particles.set_emitter(idx3, "texture_index", 1)
    local ea2 = particles.get_emitter(idx3)
    T.check("set anim_rows", ea2.anim_rows == 8.0)
    T.check("set anim_speed", ea2.anim_speed == 5.0)
    T.check("set interpolate false", ea2.interpolate == false)
    T.check("set texture_index", ea2.texture_index == 1)

    particles.remove_emitter(idx3)

    -- get_texture_names
    local texnames = particles.get_texture_names()
    T.check("get_texture_names returns table", type(texnames) == "table")

    -- remove_emitter
    particles.remove_emitter(idx2)
    T.check("remove decreases count", particles.get_count() == initial + 1)

    -- get_particle_count
    local pc = particles.get_particle_count()
    T.check("get_particle_count", type(pc) == "number")

    -- transient burst helper
    local burst_idx = particles.emit_burst({
        preset = "hero_take",
        position = vec3(0, 1, 0),
        count = 12,
        life_max = 0.25,
        cleanup_delay = 5.0
    })
    T.check("emit_burst returns index", burst_idx >= 0)
    local burst = particles.get_emitter(burst_idx)
    T.check("emit_burst creates emitter", burst ~= nil and burst.count == 12)
    particles.kill_emitter_particles(burst_idx)
    particles.remove_emitter(burst_idx)

    -- invalid index
    local bad = particles.get_emitter(999)
    T.check("invalid index returns nil", bad == nil)

    -- cleanup
    particles.remove_emitter(idx)
    T.check("cleanup restores count", particles.get_count() == initial)

    T.summary("Particle Tests")
end
