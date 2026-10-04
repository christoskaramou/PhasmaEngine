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
        cpu_report, cpu_ids = run(window(cpu))
        assert cpu_ids == {"inspect_frame"}  # CPU total may be waiting; do not reduce quality
        assert "fix_cpu_or_present_wait" in {option["id"] for option in cpu_report["options"]}
        assert "fix_cpu_or_present_wait" not in {option["id"] for option in run(window())[0]["options"]}
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

        # Frame history and worst-frame breakdowns: every frame counts, spikes are attributed.
        steady = copy.deepcopy(base)
        steady["context"]["settings"].update({"ssao": True, "taa": True, "fxaa": False})
        steady["overview"]["frame_ms"] = 10.0
        steady["gpu"] = {"total_ms": 5.0, "passes": [
            {"name": "CommandBuffer_Main_queue", "cur_ms": 5, "depth": 0},
            {"name": "SSAOPass", "cur_ms": 1.5, "depth": 1},
            {"name": "SSAO_pass", "cur_ms": 1.2, "depth": 2},
            {"name": "ShadowPass", "cur_ms": 3, "depth": 1},
            {"name": "ShadowCascade_0_pass", "cur_ms": 2.5, "depth": 2}]}
        steady["cpu"] = {"total_ms": 6.0, "scopes": [
            {"name": "Update Systems", "cur_ms": 4, "depth": 0},
            {"name": "Script System", "cur_ms": 3, "depth": 1}]}
        normal = {"frame_ms": 10.0, "cpu_total_ms": 9.0, "gpu_total_ms": 5.0}

        def spiky(cpu_spikes=(), gpu_spikes=()):
            frames = window(steady)
            for i, frame in enumerate(frames):
                frame["frame_history"] = [dict(normal) for _ in range(15)]
                frame["worst_frame"] = {**normal, "scopes": steady["cpu"]["scopes"], "passes": steady["gpu"]["passes"]}
                if i in cpu_spikes:
                    frame["frame_history"][7] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 5.0}
                    frame["worst_frame"] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 5.0,
                                            "scopes": [{"name": "Update Systems", "cur_ms": 32, "depth": 0},
                                                       {"name": "Script System", "cur_ms": 30, "depth": 1}],
                                            "passes": steady["gpu"]["passes"]}
                if i in gpu_spikes:  # CPU total rises too: it waits on the GPU
                    frame["frame_history"][7] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 30.0}
                    frame["worst_frame"] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 30.0,
                                            "scopes": steady["cpu"]["scopes"],
                                            "passes": [{"name": "CommandBuffer_Main_queue", "cur_ms": 30, "depth": 0},
                                                       {"name": "ShadowPass", "cur_ms": 25, "depth": 1},
                                                       {"name": "ShadowCascade_0_pass", "cur_ms": 24, "depth": 2}]}
            return frames

        report, ids = run(spiky(cpu_spikes=(4, 7, 10)))
        frames, spikes = report["evidence"]["frames"], report["evidence"]["spikes"]
        assert frames["count"] == 11 * 15 and frames["frame"]["median_ms"] == 10, frames  # first packet skipped
        assert spikes["count"] == 3 and spikes["cpu"] == 3 and spikes["local_median_ms"] == 10, spikes
        assert spikes["verdict"] == "insufficient" and report["jev_ready"] is False, spikes  # 1.7 s of frames
        assert spikes["median_interval_ms"] == 480 and spikes["with_breakdown"] == 3, spikes
        assert spikes["sources"] == [{"name": "Script System", "side": "cpu", "count": 3,
                                      "avg_excess_ms": 27, "max_excess_ms": 27, "avg_share": 0.9}], spikes
        assert "spike_source" in ids, ids
        options = {option["id"]: option["affected_gpu_ms"] for option in report["options"]}
        assert options["disable_ssao"] == 1.5 and options["disable_taa"] is None, options  # region, not region + nested pass
        assert "disable_fxaa" not in options and "fix_spike_1" in options and "no_change" in options, options
        assert report["evidence"]["top_cpu_scopes"][0] == {"name": "Script System", "median_ms": 3}

        spikes = run(spiky(gpu_spikes=(4, 8)))[0]["evidence"]["spikes"]
        assert spikes["gpu"] == 2 and spikes["cpu"] == 0, spikes  # GPU first: CPU totals include GPU waits
        assert spikes["sources"][0]["name"] == "ShadowCascade_0_pass" and spikes["sources"][0]["side"] == "gpu", spikes
        assert run(spiky(cpu_spikes=(0,)))[0]["evidence"]["spikes"]["count"] == 0  # connect warm-up packet
        one = run(spiky(cpu_spikes=(5,)))[0]  # one spike is evidence, not a recurring source to fix
        assert one["evidence"]["spikes"]["sources"][0]["count"] == 1
        assert not any(o["id"].startswith("fix_spike") for o in one["options"]), one["options"]
        # Stutter under the budget (ATH: ~2 ms frames, 6 ms spawn/death frames at a 60 FPS target) is a spike.
        fast = spiky()
        for i, frame in enumerate(fast):
            frame["frame_history"] = [{"frame_ms": 2.0, "cpu_total_ms": 1.8, "gpu_total_ms": 1.0} for _ in range(15)]
            frame["worst_frame"] = {**frame["frame_history"][0], "scopes": [], "passes": []}
            if i in (4, 7, 10):
                frame["frame_history"][7] = {"frame_ms": 6.0, "cpu_total_ms": 5.8, "gpu_total_ms": 1.0}
                frame["worst_frame"] = {**frame["frame_history"][7], "passes": [], "scopes": [
                    {"name": "Scene Instance Rebuild", "cur_ms": 3.5, "depth": 0},
                    {"name": "Scene Material Table", "cur_ms": 3.0, "depth": 1}]}
        evidence = run(fast)[0]["evidence"]
        assert evidence["frames"]["over_budget"] == 0 and evidence["spikes"]["local_median_ms"] == 2, evidence
        assert evidence["spikes"]["count"] == 3 and evidence["spikes"]["cpu"] == 3, evidence["spikes"]
        assert evidence["spikes"]["sources"][0]["name"] == "Scene Material Table", evidence["spikes"]
        for i in (4, 7, 10):  # no scope holds half the extra 4 ms: name the biggest, with its share
            fast[i]["worst_frame"]["scopes"] = [{"name": "Scene Instance Rebuild", "cur_ms": 1.8, "depth": 0},
                                                {"name": "Script System", "cur_ms": 1.4, "depth": 0}]
        source = run(fast)[0]["evidence"]["spikes"]["sources"][0]
        assert source["name"] == "Scene Instance Rebuild" and source["avg_share"] == 0.45, source
        later = spiky()
        for frame in later:
            frame["context"]["settings"]["ssao"] = False
            frame["capture_unix_ms"] += 12 * 250
        spikes = run(spiky(cpu_spikes=(3,)) + later)[0]["evidence"]
        assert spikes["frames"]["count"] == 11 * 15 and spikes["spikes"]["count"] == 0  # a settings change resets frames
        session = run(spiky(cpu_spikes=(3,)) + later)[0]["session"]["summary"]  # ...but not the session timeline
        assert len(session) == 1 and session[0]["spikes"] == 1 and session[0]["settings_changed"] is True, session
        assert run(spiky(cpu_spikes=(3,)))[0]["session"]["spike_events"][0]["source"] == "unattributed"  # 3 steady samples

        def long_run(spike_every=0, step_at=None, count=130, spikes_from=0):  # 130 packets of 25 frames: ~32 s
            frames = window(steady, count=count)
            for i, frame in enumerate(frames):
                ms = 30.0 if step_at is not None and i >= step_at else 10.0
                frame["frame_history"] = [{"frame_ms": ms, "cpu_total_ms": ms - 1, "gpu_total_ms": 5.0} for _ in range(25)]
                frame["worst_frame"] = {**frame["frame_history"][0], "scopes": steady["cpu"]["scopes"],
                                        "passes": steady["gpu"]["passes"]}
                if spike_every and i >= spikes_from and i % spike_every == spike_every - 1:
                    frame["frame_history"][12] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 5.0}
                    frame["worst_frame"] = {"frame_ms": 40.0, "cpu_total_ms": 39.0, "gpu_total_ms": 5.0,
                                            "scopes": [{"name": "Script System", "cur_ms": 30, "depth": 1}],
                                            "passes": steady["gpu"]["passes"]}
            return frames

        # The verdict is measured: recurring spikes remove no_change, so Jev cannot call them fine.
        recurring, _ = run(long_run(spike_every=10))
        spikes = recurring["evidence"]["spikes"]
        assert spikes["verdict"] == "recurring" and spikes["count"] == 13 and recurring["jev_ready"] is True, spikes
        recurring_options = {option["id"] for option in recurring["options"]}
        assert "no_change" not in recurring_options and "fix_spike_1" in recurring_options, recurring_options
        scattered = long_run(spike_every=10)  # recurring hitches, each from a different scope: nothing to fix
        for i, frame in enumerate(scattered):
            if frame["worst_frame"]["frame_ms"] == 40.0:
                frame["worst_frame"]["scopes"] = [{"name": f"One-off {i}", "cur_ms": 30, "depth": 1}]
        scattered_report = run(scattered)[0]
        scattered_ids = {o["id"] for o in scattered_report["options"]}
        assert scattered_report["evidence"]["spikes"]["verdict"] == "recurring", scattered_report["evidence"]["spikes"]
        assert "no_change" in scattered_ids and not any(i.startswith("fix_spike") for i in scattered_ids), scattered_ids
        smooth = run(long_run())[0]
        assert smooth["evidence"]["spikes"]["verdict"] == "smooth" and "no_change" in {o["id"] for o in smooth["options"]}
        # A fight that gets 3x heavier and stays there is not a stream of spikes: only the step's packet counts.
        assert run(long_run(step_at=70))[0]["evidence"]["spikes"]["count"] <= 25

        # The session timeline keeps every 10 s row and every spike; the trend reads the whole session.
        timeline = recurring["session"]
        assert [row["t_s"] for row in timeline["timeline"]] == [0, 10, 20, 30], timeline["timeline"]
        assert [row["spikes"] for row in timeline["timeline"]] == [4, 4, 4, 1] and timeline["trend"] == "insufficient"
        assert timeline["timeline"][1]["top_source"] == "Script System" and len(timeline["spike_events"]) == 13
        assert {event["source"] for event in timeline["spike_events"]} == {"Script System"}, timeline["spike_events"]
        rising, rising_ids = run(long_run(spike_every=5, count=260, spikes_from=130))
        assert rising["session"]["trend"] == "rising" and "spike_trend" in rising_ids, rising["session"]
        assert rising["session"]["first_half_spikes_per_minute"] == 0, rising["session"]

        # Consecutive slow frames are one hitch, timed by all of them and sided by the slowest.
        burst = long_run()
        for k in range(5):
            burst[50]["frame_history"][5 + k] = {"frame_ms": 40.0 + k, "cpu_total_ms": 39.0 + k, "gpu_total_ms": 5.0}
        burst_report = run(burst)[0]
        assert burst_report["evidence"]["spikes"]["count"] == 1 and burst_report["evidence"]["spikes"]["frames"] == 5
        event = burst_report["session"]["spike_events"][0]
        assert event["frames"] == 5 and event["duration_ms"] == 210 and event["frame_ms"] == 44, event

        # Totals over every frame: work in a fifth of the frames that adds up is intermittent and fixable;
        # work in every frame is not intermittent, and a wait is never offered as a fix.
        costly = long_run()
        for frame in costly:
            frame["totals"] = {"frames": 25, "gpu_frames": 25,
                               "cpu": [{"name": "Script System", "ms": 7.5, "frames": 25},
                                       {"name": "Prefab Instantiate", "ms": 10.0, "frames": 5},
                                       {"name": "Runtime Wait Previous Frame", "ms": 20.0, "frames": 8}],
                               "gpu": [{"name": "CommandBuffer_Main_queue", "ms": 125.0, "frames": 25},
                                       {"name": "ShadowPass", "ms": 50.0, "frames": 25}]}
        costly_report = run(costly)[0]
        costs = costly_report["evidence"]["costs"]
        assert [item["name"] for item in costs["intermittent"]] == ["Prefab Instantiate"], costs
        assert costs["intermittent"][0] == {"name": "Prefab Instantiate", "ms_per_frame": 0.4, "share_pct": 4,
                                            "in_frames_pct": 20}, costs["intermittent"]
        assert {item["name"]: item.get("wait") for item in costs["cpu"]}["Runtime Wait Previous Frame"] is True
        assert [item["name"] for item in costs["gpu"]] == ["ShadowPass"], costs["gpu"]  # command-buffer roots left out
        assert "reduce_cost_1" in {o["id"] for o in costly_report["options"]}
        assert costly_report["session"]["timeline"][0]["top_intermittent"] == "Prefab Instantiate"
        waits = long_run(spike_every=10)
        for frame in waits:
            if frame["worst_frame"]["frame_ms"] == 40.0:
                frame["worst_frame"]["scopes"] = [{"name": "Runtime Wait Previous Frame", "cur_ms": 30, "depth": 0}]
        wait_report, wait_ids = run(waits)
        assert wait_report["evidence"]["spikes"]["sources"][0]["side"] == "wait", wait_report["evidence"]["spikes"]
        assert not any(o["id"].startswith("fix_spike") for o in wait_report["options"]) and "spike_source" not in wait_ids

        # Slowdowns below the spike line add up: 5 frames a packet at 1.3x lose more time than any spike, and the
        # work that varies most frame to frame is named.
        slow = long_run()
        for frame in slow:
            for k in range(5):
                frame["frame_history"][k] = {"frame_ms": 13.0, "cpu_total_ms": 12.0, "gpu_total_ms": 5.0}
            frame["totals"] = {"frames": 25, "gpu_frames": 25,
                               "cpu": [{"name": "Script System", "ms": 7.5, "sq": 2.25, "frames": 25},
                                       {"name": "Animation", "ms": 24.0, "sq": 48.0, "frames": 12}],
                               "gpu": []}
        slow_report = run(slow)[0]
        slowdowns = slow_report["evidence"]["slowdowns"]
        assert slowdowns["frames"] == 129 * 5 and slow_report["evidence"]["spikes"]["count"] == 0, slowdowns
        assert slowdowns["extra_ms_per_minute"] > 0 and slowdowns["share_pct"] > 1, slowdowns
        variation = slow_report["evidence"]["costs"]["cpu_variation"]
        assert variation[0]["name"] == "Animation" and abs(variation[0]["sd_ms"] - 1.0) < 0.01, variation
        assert "smooth_slowdowns" in {o["id"] for o in slow_report["options"]}
        assert "smooth_slowdowns" not in {o["id"] for o in run(long_run(spike_every=10))[0]["options"]}

        # The Jev request carries the evidence and options, never the scene path.
        report = recurring
        saved = Path(directory) / "advice.json"
        saved.write_text(json.dumps(report), encoding="utf-8")
        tool = Path(__file__).with_name("jev_advise.py")
        dry = subprocess.run([sys.executable, str(tool), str(saved), "--dry"], capture_output=True, text=True)
        request = json.loads(dry.stdout)["request"]
        assert dry.returncode == 0 and "scene_path" not in dry.stdout and "sponza" not in dry.stdout, dry.stdout
        assert set(request["questions"]["next_change"]["criteria"]) == {o["id"] for o in report["options"]}
        assert request["state"]["evidence"]["spikes"]["sources"][0]["name"] == "Script System"
        assert len(request["state"]["session"]["summary"]) == 4 and "spike_events" not in dry.stdout, request["state"]["session"]
        for unready in ({"target_fps": 60}, run(spiky(cpu_spikes=(4, 7, 10)))[0]):  # no options; under 30 s of frames
            saved.write_text(json.dumps(unready), encoding="utf-8")
            empty = subprocess.run([sys.executable, str(tool), str(saved), "--dry"], capture_output=True, text=True)
            assert empty.returncode == 1 and "error" in json.loads(empty.stdout), empty.stdout
        print("Profiler Advisor: all capture, evidence, context-reset, budget, spike, option and Jev-request checks passed.")


if __name__ == "__main__":
    main()
