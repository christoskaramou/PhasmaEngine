# Phasma AI v0.1: profiler advisor

The PhasmaProfiler **AI Advisor** tab provides rule-based performance suggestions.
It does not change settings, load an ML model, or predict an FPS gain. Each
suggestion contains its measured reason, visual tradeoff and an A/B validation
step. JSON reports also carry the proposed setting/value when a tuning rule
matches, the measurements used and the capture context.

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

## Capture contract

Core `ProfilerSnapshot::CaptureMetadata` supplies the same additive metadata to
the Player stream and editor `profiler_snapshot` exports:

- `schema_version: 1`, a process capture-session identifier, and UTC Unix milliseconds;
- scene path, graphics API, GPU name, platform, logical CPU count and build configuration;
- actual present mode, display dimensions and the host's viewport render dimensions;
- selected settings: render scale, effective shadow enablement, shadow map size,
  cascade count/distance/LOD bias, mesh LOD enablement/bias, culling and Forward+.

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

GPU timestamps are asynchronous samples, not exact matches to the current CPU
frame. Recommendations are experiments to validate using the same scene/camera,
one setting at a time, with both performance and visual comparisons. The advisor
runs in the separate profiler process, never in the renderer's decision path.

## Validation

```powershell
cmake --build build-ninja --config Release --target PhasmaEditor PhasmaPlayer PhasmaProfiler
python tools/test_profiler_advice.py build-ninja/Release/PhasmaProfiler.exe
```

The test runs the shipped CLI on synthetic captures covering actual pass names,
budget changes, insufficient/duplicate samples, context/session/gap resets,
missing/inconsistent GPU timing, disabled settings, VRAM pressure, legacy/schema
mismatch input, malformed JSON, and compact-capture metadata preservation.

Source: `Phasma/Profiler/ProfilerAdvice.*`, `Phasma/Profiler/main.cpp`,
`Phasma/Core/Code/Base/ProfilerSnapshot.*`, `Phasma/Core/Code/Base/ProfilerStream.*`,
`Phasma/Runtime/Code/Runtime/PlayerHost.cpp`,
`Phasma/Editor/Code/GUI/Widgets/ProfilerWidget.cpp`, `tools/profiler_capture.py`.
