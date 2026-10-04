# Phasma AI v0.2: profiler advisor

The PhasmaProfiler **AI Advisor** tab provides rule-based performance suggestions,
every-frame CPU/GPU statistics and spike attribution, plus an opt-in **Ask Jev**
ranking. It does not change settings or predict an FPS gain. Each suggestion
contains its measured reason, visual tradeoff and an A/B validation step. JSON
reports also carry the proposed setting/value when a tuning rule matches, the
measurements used, the capture context and the `options` an external chooser
can pick from.

## Use

Launch PhasmaPlayer with `--profiler`, or enable Live profiler in the launcher.
Open PhasmaProfiler's AI Advisor tab (`PhasmaProfiler --advisor` opens it directly).
Select the target FPS with the existing Budget control. Suggestions refresh as
new snapshots arrive. **Save advice report** writes JSON under `ProfilerCaptures/`.

For longer records, use the existing stream capture and then the same advisor
without opening a window:

```powershell
python tools/profiler_capture.py --seconds 60 --out capture.jsonl
build-ninja/Release/PhasmaProfiler.exe --advise capture.jsonl --target-fps 60
```

The CLI prints a JSON report to stdout. It accepts JSONL streams or a single
snapshot JSON, returns 2 for malformed input/arguments and 0 for a report
(including an insufficient-data report). A single snapshot intentionally cannot
support a tuning recommendation. No Python package or inference runtime is added.

For A/B performance gates, the Player stream's slowest rate is `Hz4` (index 0 of
`ProfilerRefreshRate` in `Phasma/Core/Code/Base/ProfilerStream.h`; there is no
1 Hz rate), and the first packet after connecting is a warm-up artifact (about
30 fps, 0 ms GPU). A one-snapshot-per-second protocol must drop that packet and
keep one packet per second. A harness that requested index 1 (`Hz10`) and
averaged ten consecutive packets produced 0.9 s captures dominated by the
warm-up packet and an unreproducible Sponza baseline. `tools/compare_snapshots.py`
flags FPS only when the drop is both >5% and >1 fps (verified 2026-09-30).

## Ask Jev

**Ask Jev** saves the report under `ProfilerCaptures/` and runs
`tools/jev_advise.py` on it (found by walking up from the executable, so it works
from a dev tree only), off the UI thread with a 90 s limit. The tool sends one
`choice` question to `https://api.typesafe.ai/v1/systemone`: state = target FPS,
GPU/API/platform/resolution, settings and `evidence`; criteria = the report's
`options`, each with the measured GPU time of the passes it affects. Jev returns
`choice`, `confidence` and `probabilities`; the tab shows the ranking. The scene
path is never sent (the test asserts it). Jev accepts text only, so it judges
visual cost from the option descriptions, not from the frame.

Live check (2026-10-04, Sample `las_vegas_neon_showcase`, RTX 4080, ~171 fps,
present-bound): at 60 FPS Jev chose `no_change` (0.98 confidence); at 240 FPS it
chose `lower_render_scale` (0.34) until `fix_cpu_or_present_wait` existed, then
that option (0.96). Jev can only pick what `options` offers, so a missing
evidence-based option shows up as a confident-looking wrong pick. ATH Map 2 heavy
fight (bomber pilot, ~200 extra enemies, endless waves, 60 s): 34 spikes, 20 of
22 breakdowns `Scene Instance Rebuild` (CPU, +1.2 ms, 50% share, from rig
spawns/deaths); Jev chose that `fix_spike_1` (0.92). The AI Advisor tab
re-analyzes at most 4 times a second. Each call is
billed to the TypeSafe key: `JEV_API_KEY`/`TYPESAFE_API_KEY`, Credential Manager
`jev`/`api-key`, or `typesafeApiKey` in `%APPDATA%/jev-model-router/config.json`.

```powershell
python tools/jev_advise.py ProfilerCaptures/advice_<time>.json --dry   # request only, no key, no network
python tools/jev_advise.py ProfilerCaptures/advice_<time>.json         # one billed call
```

## Capture contract

Core `ProfilerSnapshot::CaptureMetadata` supplies the same additive metadata to
the Player stream and editor `profiler_snapshot` exports:

- `schema_version: 1`, a process capture-session identifier, and UTC Unix milliseconds;
- scene path, graphics API, GPU name, platform, logical CPU count and build configuration;
- actual present mode, display dimensions and the host's viewport render dimensions;
- selected settings: render scale, effective shadow enablement, shadow map size,
  cascade count/distance/LOD bias, mesh LOD enablement/bias, culling, Forward+, and
  the effective post-process toggles (SSAO, SSR, TAA, FXAA, CAS sharpening, bloom,
  DOF, motion blur) from `ActivePostProcessProfile()`, so entering a volume is a
  context change.

Every Player stream packet also carries `frame_history` (every frame since the
previous packet: frame, CPU and GPU totals) and `worst_frame`: the slowest of those
frames with its own CPU scopes and GPU passes (`ProfilerStreamServer::Tick` copies
the breakdown whenever a new slowest frame arrives). Spikes between sampled
snapshots therefore keep their attribution without changing the packet rate.

The engine gathers metadata at snapshot publication time, while profiling is
enabled, rather than serializing it every rendered frame. `--compact` preserves
metadata while filtering small CPU scopes. Existing overview/CPU/GPU/counter
fields remain compatible with older readers.

This is a measurement record, not a training dataset with verified action/outcome
labels. It does not capture every scene property, camera motion, post-process
volume state, exact CPU model or build revision. Do not mix captures from
different builds for a causal comparison without recording that information
separately. JSONL is the persistent storage; SQLite is unnecessary for v0.1.

## Evidence and rules

The advisor retains up to 40 samples, accepts at most 10 per second, and requires
at least eight samples spanning one second. It resets on a change in the
recorded context or capture session, backwards timestamps, invalid metadata,
or a gap longer than two seconds. Repeated timestamps do not accumulate evidence.
The CLI analyzes the final such window of a file; it does not average a whole
file containing different scenes/settings together.

- GPU work above 105% of the target budget, with the frame over budget and
  lighting at least 1 ms / 25% of measured GPU work: test a 10% render-scale
  reduction, with a floor of 0.5. This uses `LightOpaquePass_pass` and
  `LightTransparentPass_pass`, the actual backend timestamp names.
- The same GPU/frame pressure, with shadows enabled and `ShadowPass` at least
  1 ms / 20% of GPU work: test increasing `shadow_lod_bias` by 50%, capped at 4.
  Lower mesh LODs must exist for this to help. Nested cascade regions are not
  counted a second time.
- Frame time over budget without adequate GPU evidence: inspect CPU work and
  presentation waits. CPU total time can contain GPU waits, so the advisor does
  not label it proof of CPU computation cost.
- App VRAM at least 90% of its reported budget: inspect texture/render-target
  residency. This alone is not proof of a memory stall.
- A spike source attributed in at least two spike breakdowns: investigate it;
  lowering quality rarely removes a recurring spike.

Every-frame evidence (`evidence.frames`, `evidence.spikes`) uses all
`frame_history` frames of the window (at most 120000, ~4 min at 500 fps) and skips
the window's first packet, which carries the connect warm-up frame. A **spike** is
a frame at least twice its **local baseline** (the median of the last 64 frames up
to its packet) and 2 ms over it, whatever the budget: stutter. `frames.over_budget`
counts budget misses separately. Spikes are counted over the **last 60 s** of
frames; `spikes.verdict` is `insufficient` under 30 s (then `jev_ready` is false:
the button is disabled and `jev_advise.py` refuses without a call), `smooth` with
none, `recurring` at two or more a minute, else `occasional`.

Why (2026-10-04, ATH heavy fights): a budget-gated definition saw 0 spikes at a
~2 ms median with 34 spikes a minute of 4-12 ms; a whole-window median then
counted 1186 "spikes" (339/min) once the fight grew from 16 to 82 enemies, and Jev
answered "all ok" or "fix" depending on when it was asked. Replaying one capture
cut at 35/45/60 s now gives `recurring`, 29-35/min, top source `Scene Instance
Rebuild` (~50% share) every time. Its side is `gpu` when GPU time (within one frame
either way, since timestamps land late or early) holds at least half of the extra
time, else `cpu` when CPU time does, else `other`; GPU first, because CPU totals
include waits on the GPU. The **source** of each `worst_frame` spike is the
deepest scope (CPU) or pass (GPU, excluding command-buffer roots) whose time over
its steady median holds at least half of that side's extra time, else the one
holding the most; `avg_share` is the fraction of the extra time it explains (low
= cost spread over many scopes). A packet's worst frame is attributed once, when
it arrives, only if it is a spike, against the steady medians of that moment; the
60 s window and the session timeline reuse that one result, so they cannot
disagree. With fewer than four steady samples (just after a reset) a spike stays
`unattributed`. Same-name scopes are summed per frame; all scripts share the
`Script System` scope, so attribution stops there. Two spikes inside one packet
interval share one breakdown. `top_gpu_passes` ranks depth-1 passes by median
time and `top_cpu_scopes` ranks scopes by median self time.

Only sources seen in at least two spike breakdowns become `fix_spike_N` options
(one-off sources stay in the evidence), and a `recurring` verdict removes
`no_change`, so Jev ranks fixes instead of judging whether the stutter matters.
With one-off sources offered, Jev picked a single 2.9 ms GPU-wait spike over the
source of 8 of 9 spikes; without them it chose that source at 0.98 on both cuts.

`options` lists the changes the settings allow: disabling each enabled post
effect, render scale x0.9, shadow map/cascades/distance/LOD bias, enabling mesh
LOD or culling, fixing each attributed spike source, `fix_cpu_or_present_wait`
when the frame misses the budget while GPU work fits it, and `no_change`.
`affected_gpu_ms` is the median GPU time of the passes a change touches (a cost,
not a predicted saving; a region and its nested `*_pass` count once), or null
when unmeasured.

GPU timestamps are asynchronous samples, not exact matches to the current CPU
frame. Recommendations are experiments to validate using the same scene/camera,
one setting at a time, with both performance and visual comparisons. The advisor
runs in the separate profiler process, never in the renderer's decision path.

## Every-frame totals, hitches, slowdowns and waits

The Player stream also sends `totals` each packet: every CPU scope's and GPU pass's
**inclusive** time summed over every frame since the previous packet, the sum of
each frame's time squared (`sq`), and the number of frames it ran in
(`ProfilerStreamServer::Tick`, only while a client is connected; inclusive because
worker-thread scopes interleave in one entry list, which breaks self time; CPU keyed
by name pointer, merged by text at publish; GPU counted only when a new GPU frame
lands). From them the advisor reports, for the last minute (`evidence.costs`) and
the session (`session.costs`): each scope's `ms_per_frame`, `share_pct` and
`in_frames_pct`; `intermittent` (runs in under half the frames, 0.01 ms per frame
or more, never a wait) and `cpu_variation`/`gpu_variation` (standard deviation
over every frame). `reduce_cost_N` offers intermittent work costing 3% or more.

- **Hitches:** consecutive spike frames count once (`spikes.count`), sided and timed
  by their slowest frame; `spikes.frames` counts the frames. Rates before
  2026-10-04 evening counted frames, so older per-minute figures are higher.
- **Slowdowns:** frames 1.25-2x their local baseline but not spikes;
  `evidence.slowdowns` gives their lost milliseconds a minute next to the spikes'
  (`spikes.extra_ms_per_minute`). `smooth_slowdowns` is offered when they lose more
  than the spikes and at least 1% of frame time.
- **Waits:** a CPU culprit named like a wait (`Wait`, `Present`, `Acquire Image`,
  `Queue Submit`; a name list, a stated ceiling) gets side `wait`: the CPU blocked
  on the GPU, driver or presentation, a symptom. Waits are never fix options.
- **Options:** spike options state the time each source loses a minute (Jev
  ranks by it); `Script System` is flagged as the game's own code. `no_change`
  disappears only when hitches recur **and** a repeating source can be fixed:
  scattered hitches with no common source no longer force a settings cut.
- **Script API scopes:** `CppScript.cpp` scopes the native calls behind churn and
  effects: `Script API Instantiate`, `Destroy`, `Animation`, `Material Color`,
  `Sound`, `Particles` (not per-frame calls like positions or UI quads).
- The tab shows Jev's answer with its age and the evidence line it was asked on
  (`JevBasis`), flagged when the evidence has changed since.

ATH Map 2 heavy fight, 2026-10-04 (bomber pilot, ~200 extra enemies, 60 s, ~480
fps): effects cost ~nothing (particles 0.002 ms a call, sounds 0.014 ms, hit-flash
colour 0.006 ms per frame, animation calls 0.033 ms); the 16 spike frames were rig
**instantiates** past the pool (~2 ms, 8 frames), the **game's own code** (+2 ms in
`Script System` with no engine call, 3 frames) and **GPU waits** (the rest);
hitches lose 67 ms and slowdowns 431 ms a minute (0.8% of frame time). Jev chose
`Prefab Instantiate` (7.1 ms lost a minute) at 0.49: a close call between small
costs, which the confidence reflects. A screenshot showed an 80 s old Jev answer
beside newer evidence; that answer had matched the evidence it was given.

## Session timeline

The advisor also keeps the **whole connection**: 10 s rows built as packets arrive
(every frame into a 0.25 ms histogram, spikes by side, sources by name) and every
spike as an event (time, frame, local baseline, side, and source with share when
it was the packet's attributed worst frame). A settings change or stream gap
restarts only the 60 s window and is marked `settings_changed` on its row; a new
`capture_session` (a new game process) starts a new session. The profiler no
longer resets the advisor on reconnect.

- The report's `session` carries `duration_s`, `trend` (`rising`/`falling` when
  the second half's spike rate differs from the first by 2 a minute and 1.5x,
  `steady`, or `insufficient` under 60 s), both halves' rates and `summary`: at
  most 60 merged rows (`row_s` each) with frames, median, p99, max, over-budget
  count, spikes and the top source. A `rising` trend adds the `spike_trend` rule.
- `Analyze(targetFps, true)` (the CLI, **Save advice report**, **Ask Jev** and
  the automatic save) adds `timeline` (every 10 s row) and `spike_events`.
- The profiler saves that full report as `ProfilerCaptures/timings_<time>.json`
  whenever the stream disconnects, on exit while connected, and before **Reset
  session**: the file agents (and people) read. ~10 KB per minute of a heavy ATH
  fight.
- Jev receives only the `summary` rows and trend (`tools/jev_advise.py`), never
  the 10 s rows or events. On the 60 s ATH capture: 6 rows, 6.7 KB request,
  `fix_spike_1` at 0.99 (2026-10-04).

Ceilings: histogram percentiles and over-budget counts have 0.25 ms resolution
(the last bin is open from 64 ms); `spike_events` stops at 50000 per session.

## Validation

```powershell
cmake --build build-ninja --config Release --target PhasmaEditor PhasmaPlayer PhasmaProfiler
python tools/test_profiler_advice.py build-ninja/Release/PhasmaProfiler.exe
```

The test runs the shipped CLI on synthetic captures covering actual pass names,
budget changes, insufficient/duplicate samples, context/session/gap resets,
missing/inconsistent GPU timing, disabled settings, VRAM pressure, legacy/schema
mismatch input, malformed JSON, compact-capture metadata preservation, CPU and
GPU spike attribution, the warm-up packet skip, option costs, and a `--dry` Jev
request without the scene path (verified 2026-10-04).

Source: `Phasma/Profiler/ProfilerAdvice.*`, `Phasma/Profiler/main.cpp`,
`Phasma/Core/Code/Base/ProfilerSnapshot.*`, `Phasma/Core/Code/Base/ProfilerStream.*`,
`Phasma/Runtime/Code/Runtime/PlayerHost.cpp`,
`Phasma/Editor/Code/GUI/Widgets/ProfilerWidget.cpp`, `tools/profiler_capture.py`,
`tools/jev_advise.py`.
