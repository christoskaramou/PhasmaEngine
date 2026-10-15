"""Play the C++ CoinGrid sample with PhasmaGameDriver in a disposable Player, across a Lua reload and a module reload."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from editor_stress import connect_mcp, execute_lua, call_tool, text_content

GAME_TOOLS = {"game_describe", "game_observe", "game_act", "game_reset"}

parser = argparse.ArgumentParser()
parser.add_argument("--binary-dir", required=True, type=Path, help="a Release folder with PhasmaPlayer and PhasmaGame")
parser.add_argument("--driver", required=True, type=Path, help="PhasmaGameDriver.exe")
parser.add_argument("--port", type=int, default=8881)
parser.add_argument("--steps", type=int, default=40)
args = parser.parse_args()
binary = args.binary_dir.resolve()
url = f"http://127.0.0.1:{args.port}/mcp"


def wait_for(fn, timeout=30):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            result = fn()
            if result:
                return result
        except Exception as error:
            last = error
        time.sleep(0.25)
    raise AssertionError(f"Timed out: {last}")


def driver_command(*extra):
    # Traces stay in the disposable runtime, not beside the driver executable.
    trace = runtime / f"trace_{time.monotonic_ns()}.jsonl"
    return [str(args.driver), "--url", url, "--policy", "local", "--trace", str(trace), *extra]


def drive(*extra):
    return subprocess.run(driver_command(*extra), capture_output=True, text=True, timeout=300)


def finished(run, decisions):
    line = next((line for line in run.stdout.splitlines() if line.startswith("Finished:")), "")
    match = re.search(r"decisions=(\d+) .* score=(\d+)", line)
    assert run.returncode == 0 and match and int(match.group(1)) == decisions, (run.returncode, run.stdout, run.stderr)
    return int(match.group(2))


try:
    connect_mcp(url, 1)
except Exception:
    pass
else:
    raise RuntimeError(f"Something already serves {url}; choose another --port")

with tempfile.TemporaryDirectory(prefix="native-driver-smoke-") as temporary:
    runtime = Path(temporary)
    for item in binary.iterdir():
        library = item.suffix.lower() == ".dll" and "_live_" not in item.name  # not another process's shadow copy
        if library or item.name in ("PhasmaPlayer.exe", "NativeScripts.json"):
            shutil.copy2(item, runtime)
    for folder in ("RuntimeAssets", "MaterialX"):
        if (binary / folder).exists():
            shutil.copytree(binary / folder, runtime / folder)
    assets = runtime / "Assets"
    shutil.copytree(binary / "DefaultProject" / "Assets", assets)
    (assets / "Agent").mkdir()
    (assets / "Agent" / "agent_config.json").write_text(json.dumps({"mcp": True, "mcp_port": args.port}))
    (runtime / "phasma_settings.json").write_text(json.dumps({
        "project_path": str(assets), "project_manifest": "", "startup_scene": "Scenes/main.pescene",
        "graphics_api": "vulkan", "live_profiler": False}))

    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(runtime / "PhasmaPlayer.exe"), "--api", "vulkan", "--display", "1"],
                               cwd=runtime, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        client = wait_for(lambda: connect_mcp(url, 2), 90)

        def lua(code):
            result = json.loads(execute_lua(client, code))
            output = result.get("output", "")
            if result.get("error") or output.startswith("error:"):
                raise RuntimeError(result)
            return output.strip()

        def game_tools():
            return GAME_TOOLS <= {tool["name"] for tool in client.list_tools()}

        def observe():
            return json.loads(text_content(call_tool(client, "game_observe", {})))

        def reloads():
            return int(lua("return tostring(engine.native_scripts_status().reloads)"))

        wait_for(lambda: lua("return tostring(engine.native_scripts_status().active)") == "true")
        assert not game_tools(), "game tools exist before the C++ game is attached"
        lua('scene.add_empty_node("CoinGridHost"):set_script("cpp:CoinGrid", "player"); return "ok"')
        wait_for(game_tools)
        state = observe()
        assert state["protocol_version"] == 1 and state["game_id"] == "coin_grid" and len(state["actions"]) == 4, state

        # The driver plays to its step cap; the greedy policy collects coins.
        score = finished(drive("--seed", "7", "--steps", str(args.steps)), args.steps)
        assert score > 0, score

        # A Lua reload drops Lua tools and keeps the C++ instance and its tools (same session).
        lua('mcp.register_tool({name = "lua_probe", handler = function() return {ok = true} end}); return "ok"')
        assert json.loads(text_content(call_tool(client, "lua_probe", {}))) == {"ok": True}
        session = observe()["session_id"]
        lua('reload_scripts(); return "queued"')
        wait_for(lambda: "lua_probe" not in {tool["name"] for tool in client.list_tools()})
        assert game_tools() and observe()["session_id"] == session

        # A module reload mid-session: the running driver loses its session, the Player keeps going and the new
        # instance registers the tools again.
        before = reloads()
        driver = subprocess.Popen(driver_command("--seed", "8", "--steps", "1000", "--pace-ms", "50"),
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(3)
        game = runtime / "PhasmaGame.dll"
        os.utime(game, (time.time(), time.time() + 5))
        wait_for(lambda: reloads() > before, 30)
        stdout, stderr = driver.communicate(timeout=60)
        assert driver.returncode != 0 and "Finished:" not in stdout, (driver.returncode, stdout, stderr)
        assert process.poll() is None, "the Player exited during the module reload"
        wait_for(game_tools)
        fresh = observe()
        assert fresh["session_id"] != session and fresh["state_id"] == 1, fresh

        finished(drive("--seed", "9", "--steps", "10"), 10)

        # Deleting the script's node destroys the instance, which takes its tools with it.
        lua('scene.delete_node(scene.find_model("CoinGridHost")); return "ok"')
        wait_for(lambda: not ({tool["name"] for tool in client.list_tools()} & GAME_TOOLS), 10)
        assert process.poll() is None

        # A normal quit with the tools registered tears them down cleanly.
        lua('scene.add_empty_node("CoinGridHost"):set_script("cpp:CoinGrid", "player"); return "ok"')
        wait_for(game_tools)
        lua('engine.quit(); return "ok"')
        code = process.wait(timeout=30)
        assert code == 0, f"the Player quit with 0x{code & 0xFFFFFFFF:08X}"
        print(f"driver smoke ok: score {score} in {args.steps} steps; interrupted run: {stderr.strip()}")
    finally:
        process.kill()
        process.wait()
        log = (runtime / "PhasmaEngine.log").read_text(errors="replace").splitlines()
        flagged = [line for line in log if re.search(r"\b(error|warn|warning|fault)\b", line, re.IGNORECASE)]
        print(f"PhasmaEngine.log: {len(log)} lines, {len(flagged)} flagged")
        for line in flagged:
            print("log:", line)
