#!/usr/bin/env python3
"""Launch or reuse PhasmaEditor and drive a heavy MCP/Lua stress scenario."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
DEFAULT_BUILD_DIRS = (ROOT / "build-ninja-full", ROOT / "build")
DEFAULT_CONFIG = "Release"
DEFAULT_MCP_URL = "http://127.0.0.1:8765/mcp"
REPORT_ROOT = ROOT / "reports" / "editor_stress"
SCENARIO_PATH = ROOT / "PhasmaEditor" / "Assets" / "Scripts" / "stress" / "editor_stress.lua"
NODE_SCRIPT_PATH = ROOT / "PhasmaEditor" / "Assets" / "Scripts" / "stress" / "editor_stress_node.lua"

sys.path.insert(0, str(TOOLS))
from phasma_adk_agent.mcp_probe import McpClient, McpProbeError  # noqa: E402


PROFILES: dict[str, dict[str, Any]] = {
    "quick": {
        "primitive_count": 256,
        "scripted_nodes": 64,
        "point_lights": 32,
        "spot_lights": 16,
        "area_lights": 8,
        "directional_lights": 2,
        "emitters": 8,
        "particles_per_emitter": 512,
        "cameras": 4,
        "phases": 3,
        "snapshot_count": 3,
        "warmup": 3.0,
        "snapshot_interval": 1.0,
        "batch_size": 128,
        "light_batch_size": 64,
        "emitter_batch_size": 8,
        "shadow_map_size": 1024,
        "render_scale": 1.0,
        "startup_settle": 1.0,
        "scene_settle": 0.5,
        "batch_settle": 0.02,
        "cleanup_settle": 0.5,
        "shutdown_settle": 1.0,
    },
    "heavy": {
        "primitive_count": 1800,
        "scripted_nodes": 600,
        "point_lights": 300,
        "spot_lights": 128,
        "area_lights": 64,
        "directional_lights": 4,
        "emitters": 32,
        "particles_per_emitter": 4096,
        "cameras": 12,
        "phases": 10,
        "snapshot_count": 10,
        "warmup": 6.0,
        "snapshot_interval": 1.0,
        "batch_size": 150,
        "light_batch_size": 96,
        "emitter_batch_size": 8,
        "shadow_map_size": 2048,
        "render_scale": 1.0,
        "startup_settle": 2.0,
        "scene_settle": 1.0,
        "batch_settle": 0.05,
        "cleanup_settle": 1.0,
        "shutdown_settle": 2.0,
    },
    "absurd": {
        "primitive_count": 6000,
        "scripted_nodes": 2000,
        "point_lights": 1024,
        "spot_lights": 512,
        "area_lights": 256,
        "directional_lights": 8,
        "emitters": 96,
        "particles_per_emitter": 8192,
        "cameras": 24,
        "phases": 12,
        "snapshot_count": 10,
        "warmup": 8.0,
        "snapshot_interval": 1.0,
        "batch_size": 120,
        "light_batch_size": 96,
        "emitter_batch_size": 6,
        "shadow_map_size": 4096,
        "render_scale": 1.25,
        "startup_settle": 3.0,
        "scene_settle": 2.0,
        "batch_settle": 0.10,
        "cleanup_settle": 2.0,
        "shutdown_settle": 3.0,
    },
}


def choose_build_dir(explicit: Path | None) -> Path:
    if explicit:
        return explicit if explicit.is_absolute() else ROOT / explicit
    for path in DEFAULT_BUILD_DIRS:
        if path.exists():
            return path
    return DEFAULT_BUILD_DIRS[0]


def build_bin_dir(build_dir: Path, config: str) -> Path:
    return build_dir / config


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def startup_scene_path(lua_scene: str) -> str:
    scene = lua_scene.replace("\\", "/")
    if not scene:
        return ""
    if scene.startswith("Assets/"):
        return scene
    if scene.startswith("Scenes/"):
        return "Assets/" + scene
    return "Assets/Scenes/" + scene


def ensure_agent_config_mcp(bin_dir: Path) -> Path:
    path = bin_dir / "Assets" / "Agent" / "agent_config.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    data: dict[str, Any] = {}
    if path.exists():
        try:
            parsed = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(parsed, dict):
                data = parsed
        except json.JSONDecodeError:
            data = {}
    data["mcp"] = True
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return path


def ensure_startup_files(bin_dir: Path, lua_scene: str) -> None:
    scene = startup_scene_path(lua_scene)
    assets = bin_dir / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    (assets / "editor_config.json").write_text(json.dumps({"last_scene": scene}, indent=2), encoding="utf-8")
    (bin_dir / "phasma_settings.json").write_text(json.dumps({"startup_scene": scene}, indent=2), encoding="utf-8")


def remove_hot_reload_modules(bin_dir: Path) -> None:
    for path in bin_dir.glob("PhasmaEditorModule_*.dll"):
        try:
            path.unlink()
        except OSError:
            pass


def clear_engine_log(bin_dir: Path) -> None:
    log = bin_dir / "PhasmaEngine.log"
    try:
        log.write_text("", encoding="utf-8")
    except OSError:
        pass


def install_stress_assets(bin_dir: Path) -> None:
    if not NODE_SCRIPT_PATH.exists():
        raise RuntimeError(f"missing stress node script: {NODE_SCRIPT_PATH}")
    destination = bin_dir / "Assets" / "Scripts" / "stress" / NODE_SCRIPT_PATH.name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(NODE_SCRIPT_PATH, destination)


def build_editor(build_dir: Path, config: str, timeout: int) -> None:
    command = ["cmake", "--build", str(build_dir), "--config", config, "--target", "PhasmaEditor"]
    print("[editor_stress] building PhasmaEditor")
    subprocess.run(command, cwd=str(ROOT), check=True, timeout=timeout)


def connect_mcp(url: str, timeout: float) -> McpClient:
    client = McpClient(url, timeout)
    client.initialize()
    client.list_tools()
    return client


def launch_editor(args: argparse.Namespace, build_dir: Path, report_dir: Path) -> tuple[McpClient, subprocess.Popen[str] | None]:
    if args.reuse_running:
        try:
            client = connect_mcp(args.mcp_url, args.mcp_timeout)
            print("[editor_stress] reusing running editor MCP")
            return client, None
        except Exception:
            if args.no_launch_editor:
                raise

    if not args.reuse_running:
        try:
            connect_mcp(args.mcp_url, args.mcp_timeout)
        except Exception:
            pass
        else:
            raise RuntimeError("editor MCP is already reachable; pass --reuse-running or close the existing editor")

    if args.no_launch_editor:
        raise RuntimeError("MCP is not reachable and --no-launch-editor was passed")

    bin_dir = build_bin_dir(build_dir, args.config)
    exe = bin_dir / "PhasmaEditor.exe"
    if not exe.exists():
        raise RuntimeError(f"missing executable: {exe}")

    ensure_startup_files(bin_dir, "")
    config_path = ensure_agent_config_mcp(bin_dir)
    install_stress_assets(bin_dir)
    remove_hot_reload_modules(bin_dir)
    clear_engine_log(bin_dir)

    env = os.environ.copy()
    if args.validation:
        env["PE_VULKAN_VALIDATION"] = "1"
        env["PE_DX12_DEBUG"] = "1"
        env["PE_DX12_GBV"] = "1"
        env["PE_DX12_DRED"] = "1"

    stdout_path = report_dir / "editor_stdout.log"
    stdout = stdout_path.open("w", encoding="utf-8", errors="replace")
    command = [str(exe), "--api", args.api, "--display", str(args.display)]
    print(f"[editor_stress] launching {' '.join(command)}")
    process = subprocess.Popen(
        command,
        cwd=str(bin_dir),
        env=env,
        stdout=stdout,
        stderr=subprocess.STDOUT,
        text=True,
    )
    stdout.close()

    deadline = time.monotonic() + args.editor_start_timeout
    last_error: Exception | None = None
    client: McpClient | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited early with code 0x{process.returncode & 0xFFFFFFFF:08X}")
        try:
            client = connect_mcp(args.mcp_url, args.mcp_timeout)
            break
        except Exception as exc:
            last_error = exc
            time.sleep(1.0)

    if client is None:
        raise RuntimeError(f"timed out waiting for MCP at {args.mcp_url}: {last_error}")

    print(f"[editor_stress] MCP ready pid={process.pid} agent_config={config_path}")
    return client, process


def lua_literal(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, list):
        return "{" + ", ".join(lua_literal(v) for v in value) + "}"
    if isinstance(value, dict):
        parts = []
        for key, item in value.items():
            if isinstance(key, str) and key.replace("_", "").isalnum() and not key[0].isdigit():
                parts.append(f"{key} = {lua_literal(item)}")
            else:
                parts.append(f"[{lua_literal(key)}] = {lua_literal(item)}")
        return "{" + ", ".join(parts) + "}"
    if value is None:
        return "nil"
    raise TypeError(f"cannot convert to Lua literal: {value!r}")


def structured_content(result: dict[str, Any]) -> dict[str, Any]:
    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        return structured
    for block in result.get("content", []):
        if isinstance(block, dict) and block.get("type") == "text":
            text = block.get("text", "")
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError:
                continue
            if isinstance(parsed, dict):
                return parsed
    return {}


def text_content(result: dict[str, Any]) -> str:
    structured = structured_content(result)
    raw = structured.get("raw")
    if isinstance(raw, str):
        return raw
    chunks = []
    for block in result.get("content", []):
        if isinstance(block, dict) and block.get("type") == "text":
            chunks.append(str(block.get("text", "")))
    return "\n".join(chunks)


def call_tool(client: McpClient, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
    result = client.call_tool(name, arguments or {})
    if result.get("isError"):
        raise RuntimeError(f"{name} failed: {json.dumps(result, indent=2)}")
    text = text_content(result).strip()
    if text.startswith("error:"):
        raise RuntimeError(f"{name} failed: {text}")
    return result


def execute_lua(client: McpClient, code: str) -> str:
    result = call_tool(client, "execute_lua", {"code": code})
    return text_content(result)


def copy_tool_path(path_text: str, destination_dir: Path) -> Path | None:
    if not path_text:
        return None
    path = local_path_from_tool(path_text)
    if not path.exists():
        return None
    destination_dir.mkdir(parents=True, exist_ok=True)
    target = destination_dir / path.name
    try:
        shutil.copy2(path, target)
    except OSError:
        return None
    return target


def local_path_from_tool(path_text: str) -> Path:
    path = Path(path_text)
    if path.exists():
        return path
    if os.name != "nt" and len(path_text) >= 3 and path_text[1] == ":" and path_text[2] in ("/", "\\"):
        drive = path_text[0].lower()
        rest = path_text[3:].replace("\\", "/")
        return Path("/mnt") / drive / rest
    return path


def apply_overrides(args: argparse.Namespace) -> dict[str, Any]:
    config = dict(PROFILES[args.profile])
    for key in (
        "primitive_count",
        "scripted_nodes",
        "point_lights",
        "spot_lights",
        "area_lights",
        "directional_lights",
        "emitters",
        "particles_per_emitter",
        "cameras",
        "phases",
        "snapshot_count",
        "snapshot_interval",
        "warmup",
        "batch_size",
        "light_batch_size",
        "emitter_batch_size",
        "shadow_map_size",
        "render_scale",
        "startup_settle",
        "scene_settle",
        "batch_settle",
        "cleanup_settle",
        "shutdown_settle",
    ):
        value = getattr(args, key)
        if value is not None:
            config[key] = value

    config["scene"] = args.scene
    config["load_scene"] = bool(args.scene) and not args.no_scene
    config["draw_aabbs"] = not args.no_aabbs
    config["activate_stress_camera"] = args.activate_stress_camera
    config["render_mode"] = args.render_mode
    return config


def validate_config(config: dict[str, Any], allow_extreme: bool) -> None:
    primitive_count = int(config["primitive_count"])
    scripted_nodes = int(config["scripted_nodes"])
    total_particles = int(config["emitters"]) * int(config["particles_per_emitter"])
    total_lights = sum(int(config[k]) for k in ("point_lights", "spot_lights", "area_lights", "directional_lights"))
    if scripted_nodes > primitive_count:
        raise RuntimeError("--scripted-nodes cannot exceed --primitive-count")
    if min(primitive_count, scripted_nodes, total_particles, total_lights) < 0:
        raise RuntimeError("stress counts must be non-negative")
    for key in ("startup_settle", "scene_settle", "batch_settle", "cleanup_settle", "shutdown_settle"):
        if float(config[key]) < 0.0:
            raise RuntimeError(f"--{key.replace('_', '-')} must be non-negative")

    if not allow_extreme:
        reasons = []
        if primitive_count > 7000:
            reasons.append("primitive_count > 7000")
        if scripted_nodes > 2500:
            reasons.append("scripted_nodes > 2500")
        if total_particles > 1_000_000:
            reasons.append("total particles > 1,000,000")
        if total_lights > 2500:
            reasons.append("total lights > 2500")
        if reasons:
            joined = ", ".join(reasons)
            raise RuntimeError(f"refusing extreme stress settings without --allow-extreme ({joined})")


def collect_snapshot(client: McpClient, destination_dir: Path) -> Path | None:
    result = call_tool(client, "profiler_snapshot")
    data = structured_content(result)
    path = data.get("path")
    if not isinstance(path, str):
        return None
    return copy_tool_path(path, destination_dir)


def collect_screenshot(client: McpClient, destination_dir: Path) -> tuple[dict[str, Any], Path | None]:
    result = call_tool(client, "take_screenshot")
    data = structured_content(result)
    path = data.get("path")
    copied = copy_tool_path(path, destination_dir) if isinstance(path, str) else None
    return data, copied


def drive_stress(client: McpClient, config: dict[str, Any], report_dir: Path, args: argparse.Namespace) -> dict[str, Any]:
    scenario = SCENARIO_PATH.read_text(encoding="utf-8")
    lua_log = report_dir / "lua_output.log"

    def settle(label: str, seconds: float, *, quiet: bool = False) -> None:
        if seconds <= 0.0:
            return
        if not quiet:
            print(f"[editor_stress] settling {label} for {seconds:.2f}s")
        time.sleep(seconds)

    def run_lua(label: str, code: str) -> str:
        output = execute_lua(client, code)
        with lua_log.open("a", encoding="utf-8") as log:
            log.write(f"\n## {datetime.now().isoformat(timespec='seconds')} {label}\n")
            log.write(output.rstrip() if output else "<no output>")
            log.write("\n")
        return output

    startup_settle = float(config["startup_settle"])
    scene_settle = float(config["scene_settle"])
    batch_settle = float(config["batch_settle"])
    cleanup_settle = float(config["cleanup_settle"])

    settle("editor startup", startup_settle)
    run_lua("load scenario", scenario)
    run_lua("begin", f"editor_stress.begin({lua_literal(config)})")
    settle("scene setup", scene_settle)
    if batch_settle > 0.0:
        print(f"[editor_stress] settling {batch_settle:.2f}s between mutation batches")

    batch_size = int(config["batch_size"])
    for _ in range(0, int(config["primitive_count"]), batch_size):
        run_lua("spawn primitives", f"editor_stress.spawn_primitives({batch_size})")
        settle("batch", batch_settle, quiet=True)

    light_batch = int(config["light_batch_size"])
    for name in ("point_lights", "spot_lights", "area_lights", "directional_lights"):
        function = {
            "point_lights": "spawn_point_lights",
            "spot_lights": "spawn_spot_lights",
            "area_lights": "spawn_area_lights",
            "directional_lights": "spawn_directional_lights",
        }[name]
        for _ in range(0, int(config[name]), light_batch):
            run_lua(function.replace("_", " "), f"editor_stress.{function}({light_batch})")
            settle("batch", batch_settle, quiet=True)

    emitter_batch = int(config["emitter_batch_size"])
    for _ in range(0, int(config["emitters"]), emitter_batch):
        run_lua("spawn particles", f"editor_stress.spawn_particles({emitter_batch})")
        settle("batch", batch_settle, quiet=True)

    run_lua("spawn cameras", "editor_stress.spawn_cameras()")
    settle("batch", batch_settle, quiet=True)
    run_lua("spawn complete sample", "editor_stress.sample('spawn_complete')")

    print(f"[editor_stress] warming up for {config['warmup']}s")
    time.sleep(float(config["warmup"]))

    snapshot_dir = report_dir / "profiler_snapshots"
    screenshot_dir = report_dir / "screenshots"
    snapshots: list[str] = []
    phase_count = int(config["phases"])
    snapshot_count = int(config["snapshot_count"])
    interval = float(config["snapshot_interval"])

    for index in range(snapshot_count):
        phase = (index % phase_count) + 1
        run_lua(f"phase {phase}", f"editor_stress.step({phase})")
        time.sleep(interval)
        copied = collect_snapshot(client, snapshot_dir)
        if copied:
            snapshots.append(rel(copied))
            print(f"[editor_stress] snapshot {index + 1}/{snapshot_count}: {copied}")
        else:
            print(f"[editor_stress] snapshot {index + 1}/{snapshot_count}: no path returned")

    screenshot_data: dict[str, Any] = {}
    screenshot_path: Path | None = None
    if not args.no_screenshot:
        screenshot_data, screenshot_path = collect_screenshot(client, screenshot_dir)
        print(f"[editor_stress] screenshot: {screenshot_path or screenshot_data}")

    console = call_tool(client, "get_console_log", {"count": args.console_lines, "level": "all"})
    (report_dir / "console_log.json").write_text(json.dumps(structured_content(console), indent=2), encoding="utf-8")

    query_scene = call_tool(client, "query_scene")
    (report_dir / "query_scene.json").write_text(json.dumps(structured_content(query_scene), indent=2), encoding="utf-8")

    if args.hot_reload:
        call_tool(client, "reload_module")
        time.sleep(2.0)
        run_lua(
            "after hot reload sample",
            "local m = engine.get_metrics(); "
            "pe_log(string.format('[EDITOR_STRESS_SAMPLE] label=after_hot_reload fps=%.2f frame_ms=%.3f models=%d present=%s', "
            "m.fps, m.delta_ms, scene.get_model_count(), rhi.get_present_mode()))",
        )

    if args.cleanup:
        run_lua("cleanup", "editor_stress.cleanup()")
        settle("cleanup", cleanup_settle)

    return {
        "snapshots": snapshots,
        "screenshot": rel(screenshot_path) if screenshot_path else None,
        "screenshot_metadata": screenshot_data,
        "lua_output": rel(lua_log),
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="heavy")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--build", action="store_true", help="Build PhasmaEditor before launch")
    parser.add_argument("--build-timeout", type=int, default=1800)
    parser.add_argument("--api", choices=["vulkan", "dx12"], default="vulkan")
    parser.add_argument("--display", type=int, default=1)
    parser.add_argument("--mcp-url", default=DEFAULT_MCP_URL)
    parser.add_argument("--mcp-timeout", type=float, default=30.0)
    parser.add_argument("--editor-start-timeout", type=float, default=90.0)
    parser.add_argument("--reuse-running", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--no-launch-editor", action="store_true")
    parser.add_argument("--keep-editor", action="store_true", help="Leave a freshly launched editor running")
    parser.add_argument("--validation", action="store_true", help="Enable backend validation environment variables")
    parser.add_argument("--scene", default="", help="Optional scene path to load before generated stress content")
    parser.add_argument("--no-scene", action="store_true", help="Ignore --scene and run with generated stress content only")
    parser.add_argument("--no-aabbs", action="store_true", help="Disable AABB overlay pressure")
    parser.add_argument("--activate-stress-camera", action="store_true")
    parser.add_argument("--render-mode", choices=["raster", "hybrid", "ray_tracing"], default="hybrid")
    parser.add_argument("--hot-reload", action="store_true", help="Reload the editor module after snapshots")
    parser.add_argument("--cleanup", action="store_true", help="Clear stress content before exiting")
    parser.add_argument("--no-screenshot", action="store_true")
    parser.add_argument("--console-lines", type=int, default=800)
    parser.add_argument("--allow-extreme", action="store_true")
    parser.add_argument("--report-dir", type=Path)

    for key in (
        "primitive_count",
        "scripted_nodes",
        "point_lights",
        "spot_lights",
        "area_lights",
        "directional_lights",
        "emitters",
        "particles_per_emitter",
        "cameras",
        "phases",
        "snapshot_count",
        "batch_size",
        "light_batch_size",
        "emitter_batch_size",
        "shadow_map_size",
    ):
        parser.add_argument("--" + key.replace("_", "-"), type=int)
    parser.add_argument("--snapshot-interval", type=float)
    parser.add_argument("--warmup", type=float)
    parser.add_argument("--render-scale", type=float)
    parser.add_argument("--startup-settle", type=float, help="Seconds to wait after MCP connects before scene mutation")
    parser.add_argument("--scene-settle", type=float, help="Seconds to wait after scene setup before spawning stress content")
    parser.add_argument("--batch-settle", type=float, help="Seconds to wait between batched mutation calls")
    parser.add_argument("--cleanup-settle", type=float, help="Seconds to wait after cleanup before collecting the final report")
    parser.add_argument("--shutdown-settle", type=float, help="Seconds to wait before terminating a freshly launched editor")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    build_dir = choose_build_dir(args.build_dir)
    report_dir = args.report_dir or (REPORT_ROOT / datetime.now().strftime("%Y%m%d_%H%M%S"))
    report_dir.mkdir(parents=True, exist_ok=True)

    config = apply_overrides(args)
    try:
        validate_config(config, args.allow_extreme)
    except RuntimeError as exc:
        print(f"editor_stress: {exc}", file=sys.stderr)
        return 2

    (report_dir / "config.json").write_text(json.dumps(config, indent=2), encoding="utf-8")
    process: subprocess.Popen[str] | None = None

    try:
        if args.build:
            build_editor(build_dir, args.config, args.build_timeout)
        client, process = launch_editor(args, build_dir, report_dir)
        artifacts = drive_stress(client, config, report_dir, args)
        report = {
            "profile": args.profile,
            "api": args.api,
            "display": args.display,
            "build_dir": str(build_dir),
            "config": config,
            "artifacts": artifacts,
            "report_dir": str(report_dir),
        }
        (report_dir / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"[editor_stress] report: {report_dir}")
        return 0
    except (RuntimeError, McpProbeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print(f"editor_stress failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if process and not args.keep_editor:
            shutdown_settle = float(config.get("shutdown_settle", 0.0))
            if shutdown_settle > 0.0:
                print(f"[editor_stress] settling before editor shutdown for {shutdown_settle:.2f}s")
                time.sleep(shutdown_settle)
            process.terminate()
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    raise SystemExit(main())
