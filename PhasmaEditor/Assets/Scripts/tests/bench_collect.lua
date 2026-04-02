bench_collect = bench_collect or {}

function bench_collect.sample(label)
    label = label or "s"

    local metrics = engine.get_metrics()
    local mem = rhi.get_gpu_memory()

    local vram_used = mem and mem.vram and mem.vram.used
    local vram_mb = vram_used and math.floor(vram_used / (1024 * 1024)) or -1

    pe_log(string.format(
        "[BENCH_SAMPLE] label=%s fps=%.2f frame_ms=%.3f vram_mb=%d",
        label,
        metrics.fps,
        metrics.delta_ms,
        vram_mb
    ))
end

pe_log("[bench] bench_collect.lua loaded")
