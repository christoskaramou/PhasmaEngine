#!/usr/bin/env python3
"""Run the shipped Advisor CLI against representative captures; no running engine required.

python tools/test_profiler_advice.py build-ninja/Release/PhasmaProfiler.exe
"""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from profiler_capture import compact_snapshot


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="phasma-advice-") as directory:
        capture = Path(directory) / "capture.jsonl"

        def run(frames, fps=60):
            capture.write_text("\n".join(json.dumps(frame) for frame in frames), encoding="utf-8")
            result = subprocess.run([str(executable), "--advise", str(capture), "--target-fps", str(fps)],
                                    capture_output=True, text=True, check=True)
            report = json.loads(result.stdout)
            assert report["mode"] == "recommendations_only"
            for suggestion in report["recommendations"]:
                assert all(suggestion[key] for key in ("reason", "tradeoff", "validation"))
            return report, {item["id"] for item in report["recommendations"]}

        base = {
            "schema_version": 1, "capture_session": "test",
            "context": {"scene_path": "Scenes/sponza.pescene", "gpu_name": "test GPU",
                        "settings": {"render_scale": 1.0, "shadows": True, "shadow_lod_bias": 1.0}},
            "overview": {"frame_ms": 30.0, "cpu_total_ms": 28.0,
                         "memory": {"gpu_vram_app_mb": 1000, "gpu_vram_budget_mb": 8000}},
            "gpu": {"total_ms": 25.0, "passes": [
                {"name": "CommandBuffer_Main_queue", "cur_ms": 25, "depth": 0},
                {"name": "LightOpaquePass_pass", "cur_ms": 12, "depth": 1},
                {"name": "ShadowPass", "cur_ms": 6, "depth": 1},
                {"name": "ShadowCascade_0_pass", "cur_ms": 6, "depth": 2}]},
        }

        def window(template=base, count=12, step=250):
            frames = [copy.deepcopy(template) for _ in range(count)]
            for i, frame in enumerate(frames):
                frame["capture_unix_ms"] = 1_800_000_000_000 + i * step
            return frames

        report, ids = run(window())
        assert ids == {"lighting_render_scale", "shadow_lod"}, report
        changes = {item["suggested_setting"]["name"]: item["suggested_setting"]["proposed"]
                   for item in report["recommendations"]}
        assert changes == {"render_scale": 0.9, "shadow_lod_bias": 1.5}
        assert report["evidence"]["shadow_median_ms"] == 6  # nested cascades are not double-counted
        assert run(window(), fps=30)[1] == set()  # target budget, not high cost alone
        assert run(window(count=3))[1] == set()
        assert run(window(step=0))[0]["sample_count"] == 1  # duplicates are not independent evidence
        assert run(window(count=200, step=10))[0]["sample_count"] == 20  # bounded analysis cadence

        frames = window()
        frames[-1]["context"]["settings"]["render_scale"] = 0.5
        assert run(frames)[0]["sample_count"] == 1
        frames = window()
        frames[-1]["capture_session"] = "new process"
        assert run(frames)[0]["sample_count"] == 1
        frames = window()
        frames[-1]["capture_unix_ms"] += 10000
        assert run(frames)[0]["sample_count"] == 1

        cpu = copy.deepcopy(base)
        cpu["gpu"] = {"total_ms": 3, "passes": []}
        assert run(window(cpu))[1] == {"inspect_frame"}  # CPU total may be waiting; do not reduce quality
        cpu["gpu"] = {}
        missing_report, missing_ids = run(window(cpu))
        assert missing_ids == {"inspect_frame"}
        assert missing_report["evidence"]["gpu_timing_valid"] is False
        inconsistent = copy.deepcopy(base)
        inconsistent["gpu"]["total_ms"] = 1
        assert run(window(inconsistent))[1] == {"inspect_frame"}
        off = copy.deepcopy(base)
        off["context"]["settings"]["shadows"] = False
        off["context"]["settings"]["render_scale"] = 0.5
        assert run(window(off))[1] == {"inspect_gpu"}
        memory = copy.deepcopy(cpu)
        memory["overview"]["memory"]["gpu_vram_app_mb"] = 7600
        assert "vram_pressure" in run(window(memory))[1]

        old = copy.deepcopy(base)
        del old["context"]
        assert run(window(old))[1] == set()
        newer = copy.deepcopy(base)
        newer["schema_version"] = 2
        assert run(window(newer))[1] == set()
        assert run([compact_snapshot(frame, 0.05) for frame in window()])[1] == ids
        invalid = window()
        invalid[-1]["overview"]["frame_ms"] = -1
        assert run(invalid)[1] == set()
        capture.write_text('{broken JSON\n', encoding="utf-8")
        assert subprocess.run([str(executable), "--advise", str(capture)], capture_output=True).returncode == 2
        print("Profiler Advisor: all capture, evidence, context-reset, budget and invalid-input checks passed.")


if __name__ == "__main__":
    main()
