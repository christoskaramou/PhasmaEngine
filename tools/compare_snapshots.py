#!/usr/bin/env python3
"""compare_snapshots.py — diff two profiler snapshot JSONs and flag regressions.

Usage:
    python3 tools/compare_snapshots.py <baseline> <current>

Each argument is either a path to a single .json file or a directory whose
*.json files are averaged into a single synthetic snapshot before comparing.

Exit codes:
    0 — no regressions
    1 — regressions detected
    2 — usage error
"""

import json
import os
import sys
from glob import glob

# ---------------------------------------------------------------------------
# Thresholds
# ---------------------------------------------------------------------------
REGRESSION_PCT = 5.0   # >5% worse
REGRESSION_ABS_MS = 0.5   # >0.5 ms worse (applies to all _ms fields)
REGRESSION_ABS_MB = 50.0  # >50 MB worse  (applies to gpu_vram_app_mb)
REGRESSION_ABS_FPS = 1.0  # >1 fps worse  (fps is "worse" when it decreases)


# ---------------------------------------------------------------------------
# Loading / averaging
# ---------------------------------------------------------------------------

def _load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _avg(values: list) -> float:
    return sum(values) / len(values) if values else 0.0


def _sample_ms(sample: dict) -> float:
    return sample.get("ms", sample.get("cur_ms", sample.get("time_ms", 0.0)))


def _average_snapshots(snapshots: list) -> dict:
    """Merge a list of snapshot dicts by averaging all numeric fields."""
    n = len(snapshots)
    if n == 0:
        raise ValueError("No snapshots to average")

    # overview scalars
    ov_keys = ["fps", "frame_ms", "cpu_total_ms", "cpu_update_ms", "cpu_draw_ms", "gpu_total_ms"]
    overview = {k: _avg([s["overview"][k] for s in snapshots]) for k in ov_keys}

    # overview.memory — average every numeric field present in the first snapshot
    mem_keys = list(snapshots[0].get("overview", {}).get("memory", {}).keys())
    memory = {k: _avg([s.get("overview", {}).get("memory", {}).get(k, 0) for s in snapshots]) for k in mem_keys}
    overview["memory"] = memory

    # gpu.passes — group by name, average time_ms
    passes_by_name: dict = {}
    for s in snapshots:
        for p in s.get("gpu", {}).get("passes", []):
            passes_by_name.setdefault(p["name"], []).append(_sample_ms(p))
    gpu_passes = [{"name": nm, "ms": _avg(vals)} for nm, vals in passes_by_name.items()]

    gpu_total = _avg([s["gpu"]["total_ms"] for s in snapshots])

    # cpu.scopes — group by name, average time_ms
    scopes_by_name: dict = {}
    for s in snapshots:
        for sc in s.get("cpu", {}).get("scopes", []):
            scopes_by_name.setdefault(sc["name"], []).append(_sample_ms(sc))
    cpu_scopes = [{"name": nm, "ms": _avg(vals)} for nm, vals in scopes_by_name.items()]

    cpu_total = _avg([s["cpu"]["total_ms"] for s in snapshots])

    counters_by_name: dict = {}
    for s in snapshots:
        for counter in s.get("counters", []):
            counters_by_name.setdefault(counter["name"], []).append(counter.get("value", 0.0))
    counters = [{"name": nm, "value": _avg(vals)} for nm, vals in counters_by_name.items()]

    return {
        "overview": overview,
        "gpu": {"total_ms": gpu_total, "passes": gpu_passes},
        "cpu": {"total_ms": cpu_total, "scopes": cpu_scopes},
        "counters": counters,
    }


def load_input(path: str) -> dict:
    """Return a (possibly averaged) snapshot dict for path (file or dir)."""
    if os.path.isdir(path):
        json_files = sorted(glob(os.path.join(path, "*.json")))
        if not json_files:
            print(f"Error: no .json files found in directory: {path}", file=sys.stderr)
            sys.exit(2)
        print(f"Averaging {len(json_files)} snapshots from {path}")
        snapshots = [_load_json(f) for f in json_files]
        return _average_snapshots(snapshots)
    elif os.path.isfile(path):
        return _load_json(path)
    else:
        print(f"Error: path not found: {path}", file=sys.stderr)
        sys.exit(2)


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------

def _fmt_delta_ms(delta: float) -> str:
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.3f}ms"


def _fmt_pct(pct: float) -> str:
    sign = "+" if pct >= 0 else ""
    return f"{sign}{pct:.1f}%"


def _fmt_delta_fps(delta: float) -> str:
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.2f}fps"


def _fmt_delta_mb(delta: float) -> str:
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.0f}MB"


def _fmt_delta_count(delta: float) -> str:
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.0f}"


def _regression_suffix(is_regression: bool) -> str:
    return "  *** REGRESSION ***" if is_regression else ""


# ---------------------------------------------------------------------------
# Individual metric comparison
# ---------------------------------------------------------------------------

def _compare_fps(baseline: float, current: float, regression_count: list) -> str:
    """Return formatted line for fps."""
    label = "fps:"
    if baseline == 0.0:
        line = f"  {label:<20} {current:.1f}  (no baseline)"
        return line

    delta = current - baseline
    pct = (delta / baseline) * 100.0

    # Regression: fps decreases (delta negative) by >5% OR >1 fps
    is_regression = delta < 0 and (abs(pct) > REGRESSION_PCT or abs(delta) > REGRESSION_ABS_FPS)
    if is_regression:
        regression_count[0] += 1

    line = (
        f"  {label:<20} {baseline:.2f} -> {current:.2f} "
        f" ({_fmt_delta_fps(delta)}, {_fmt_pct(pct)})"
        f"{_regression_suffix(is_regression)}"
    )
    return line


def _compare_ms(label: str, baseline: float, current: float,
                regression_count: list, label_width: int = 20) -> str:
    """Return formatted line for an _ms field."""
    if baseline == 0.0:
        return f"  {label:<{label_width}} {current:.3f}  (no baseline)"

    delta = current - baseline
    pct = (delta / baseline) * 100.0

    # Regression: both >5% AND >0.5ms worse (i.e. delta positive)
    is_regression = delta > 0 and pct > REGRESSION_PCT and delta > REGRESSION_ABS_MS
    if is_regression:
        regression_count[0] += 1

    line = (
        f"  {label:<{label_width}} {baseline:.3f} -> {current:.3f}ms "
        f" ({_fmt_delta_ms(delta)}, {_fmt_pct(pct)})"
        f"{_regression_suffix(is_regression)}"
    )
    return line


def _compare_vram(label: str, baseline: float, current: float,
                  regression_count: list) -> str:
    """Return formatted line for gpu_vram_app_mb."""
    if baseline == 0.0:
        return f"  {label:<20} {current:.0f}  (no baseline)"

    delta = current - baseline
    pct = (delta / baseline) * 100.0

    # Regression: both >5% AND >50MB worse
    is_regression = delta > 0 and pct > REGRESSION_PCT and delta > REGRESSION_ABS_MB
    if is_regression:
        regression_count[0] += 1

    line = (
        f"  {label:<20} {baseline:.0f} -> {current:.0f} "
        f" ({_fmt_delta_mb(delta)}, {_fmt_pct(pct)})"
        f"{_regression_suffix(is_regression)}"
    )
    return line


# ---------------------------------------------------------------------------
# Section printers
# ---------------------------------------------------------------------------

def print_overview(baseline: dict, current: dict, regression_count: list) -> None:
    print("=== Overview ===")

    b_ov = baseline["overview"]
    c_ov = current["overview"]

    print(_compare_fps(b_ov["fps"], c_ov["fps"], regression_count))

    for key in ["frame_ms", "cpu_total_ms", "cpu_update_ms", "cpu_draw_ms", "gpu_total_ms"]:
        label = key + ":"
        print(_compare_ms(label, b_ov.get(key, 0.0), c_ov.get(key, 0.0),
                          regression_count, label_width=20))

    b_mem = b_ov.get("memory", {})
    c_mem = c_ov.get("memory", {})
    label = "gpu_vram_app_mb:"
    print(_compare_vram(label,
                        b_mem.get("gpu_vram_app_mb", 0.0),
                        c_mem.get("gpu_vram_app_mb", 0.0),
                        regression_count))


def print_gpu_passes(baseline: dict, current: dict, regression_count: list) -> None:
    print("\n=== GPU Passes ===")

    b_passes = {p["name"]: _sample_ms(p) for p in baseline.get("gpu", {}).get("passes", [])}
    c_passes = {p["name"]: _sample_ms(p) for p in current.get("gpu", {}).get("passes", [])}

    all_names = list(b_passes.keys())
    for nm in c_passes:
        if nm not in b_passes:
            all_names.append(nm)

    if not all_names:
        print("  (no passes)")
        return

    for nm in all_names:
        label = nm + ":"
        if nm not in b_passes:
            print(f"  {label:<10} {c_passes[nm]:.3f}ms  (new)")
        elif nm not in c_passes:
            print(f"  {label:<10} {b_passes[nm]:.3f}ms  (removed)")
        else:
            print(_compare_ms(label, b_passes[nm], c_passes[nm],
                              regression_count, label_width=10))


def print_cpu_scopes(baseline: dict, current: dict, regression_count: list) -> None:
    print("\n=== CPU Scopes ===")

    b_scopes = {s["name"]: _sample_ms(s) for s in baseline.get("cpu", {}).get("scopes", [])}
    c_scopes = {s["name"]: _sample_ms(s) for s in current.get("cpu", {}).get("scopes", [])}

    all_names = list(b_scopes.keys())
    for nm in c_scopes:
        if nm not in b_scopes:
            all_names.append(nm)

    if not all_names:
        print("  (no scopes)")
        return

    for nm in all_names:
        label = nm + ":"
        if nm not in b_scopes:
            print(f"  {label:<10} {c_scopes[nm]:.3f}ms  (new)")
        elif nm not in c_scopes:
            print(f"  {label:<10} {b_scopes[nm]:.3f}ms  (removed)")
        else:
            print(_compare_ms(label, b_scopes[nm], c_scopes[nm],
                              regression_count, label_width=10))


def print_counters(baseline: dict, current: dict) -> None:
    print("\n=== Counters ===")

    b_counters = {c["name"]: float(c.get("value", 0.0)) for c in baseline.get("counters", [])}
    c_counters = {c["name"]: float(c.get("value", 0.0)) for c in current.get("counters", [])}

    all_names = list(b_counters.keys())
    for nm in c_counters:
        if nm not in b_counters:
            all_names.append(nm)

    if not all_names:
        print("  (no counters)")
        return

    for nm in all_names:
        label = nm + ":"
        if nm not in b_counters:
            print(f"  {label:<10} {c_counters[nm]:.0f}  (new)")
        elif nm not in c_counters:
            print(f"  {label:<10} {b_counters[nm]:.0f}  (removed)")
        elif b_counters[nm] == 0.0:
            print(f"  {label:<10} {c_counters[nm]:.0f}  (no baseline)")
        else:
            delta = c_counters[nm] - b_counters[nm]
            pct = (delta / b_counters[nm]) * 100.0
            print(
                f"  {label:<10} {b_counters[nm]:.0f} -> {c_counters[nm]:.0f} "
                f"({_fmt_delta_count(delta)}, {_fmt_pct(pct)})"
            )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: python3 tools/compare_snapshots.py <baseline> <current>",
              file=sys.stderr)
        sys.exit(2)

    baseline_path = sys.argv[1]
    current_path = sys.argv[2]

    baseline = load_input(baseline_path)
    current = load_input(current_path)

    # regression_count is a one-element list so helpers can mutate it
    regression_count = [0]

    print_overview(baseline, current, regression_count)
    print_gpu_passes(baseline, current, regression_count)
    print_cpu_scopes(baseline, current, regression_count)
    print_counters(baseline, current)

    print()
    if regression_count[0] == 0:
        print("No regressions.")
        sys.exit(0)
    else:
        print(f"{regression_count[0]} regression{'s' if regression_count[0] != 1 else ''} detected.")
        sys.exit(1)


if __name__ == "__main__":
    main()
