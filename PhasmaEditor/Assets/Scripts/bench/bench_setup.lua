-- bench_setup.lua
-- Standard benchmark scene library for PhasmaEngine performance testing.
-- Load via dofile("bench/bench_setup.lua") before taking profiler snapshots.
--
-- API summary:
--   bench.clear()                   -- clear scene
--   bench.setup_geometry([count])   -- spawn N cubes (default 64)
--   bench.setup_rt()                -- 32 transmission cubes
--   bench.setup_alpha()             -- 32 alpha_blend cubes
--   bench.report([label])           -- log fps/frame/vram metrics
--   bench.snapshot([label])         -- save profiler snapshot, return path

bench = bench or {}

-- Clear the scene.
-- Note: resource cleanup may take a frame or two.
-- Callers should wait ~1.5s before issuing setup commands.
function bench.clear()
    scene.clear()
    pe_log("[bench] scene cleared (async — wait 1.5s before setup)")
end

-- Spawn `count` cubes (default 64).
function bench.setup_geometry(count)
    count = count or 64
    for i = 1, count do
        primitives.cube()
    end
    pe_log("[bench] setup_geometry: " .. count .. " cubes spawned")
end

-- Spawn 32 cubes with transmission material (metallic=0, roughness=0.1, ior=1.5).
function bench.setup_rt()
    local n = 32
    local nodes = {}
    for i = 1, n do
        nodes[i] = primitives.cube()
    end
    for _, node in ipairs(nodes) do
        material.set_render_type(node, "transmission")
        material.set(node, "metallic", 0.0)
        material.set(node, "roughness", 0.1)
        material.set(node, "ior", 1.5)
    end
    pe_log("[bench] setup_rt: " .. n .. " cubes with transmission material")
end

-- Spawn 32 cubes with alpha_blend render type.
function bench.setup_alpha()
    local n = 32
    local nodes = {}
    for i = 1, n do
        nodes[i] = primitives.cube()
    end
    for _, node in ipairs(nodes) do
        material.set_render_type(node, "alpha_blend")
    end
    pe_log("[bench] setup_alpha: " .. n .. " cubes with alpha_blend material")
end

-- Log current fps, frame time, and VRAM usage.
-- label defaults to "bench".
function bench.report(label)
    label = label or "bench"
    local m = engine.get_metrics()
    local mem = rhi.get_gpu_memory()
    local vram_used = mem and mem.vram and mem.vram.used
    local vram_mb = vram_used and math.floor(vram_used / (1024 * 1024)) or -1
    pe_log(string.format("[bench:%s] fps=%.1f  frame=%.2fms  vram=%d MB",
        label, m.fps, m.delta_ms, vram_mb))
end

-- Save a profiler snapshot and return the file path.
-- label defaults to "bench".
function bench.snapshot(label)
    label = label or "bench"
    local path = profiler_snapshot()
    pe_log("[bench:snapshot] " .. label .. " -> " .. (path or "<nil — is Profiler panel open?>"))
    return path
end

pe_log("[bench] bench_setup.lua loaded")
