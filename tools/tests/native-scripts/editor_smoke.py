"""Exercise actual native scripts in a disposable editor process; restore its game DLL afterwards."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from editor_stress import connect_mcp, execute_lua, call_tool

parser = argparse.ArgumentParser()
parser.add_argument("--binary-dir", required=True, type=Path)
parser.add_argument("--probe-dir", required=True, type=Path)
args = parser.parse_args()
binary = args.binary_dir.resolve()
probes = args.probe_dir.resolve()
game = binary / "PhasmaGame.dll"


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


try:
    connect_mcp("http://127.0.0.1:8765/mcp", 1)
except Exception:
    pass
else:
    raise RuntimeError("An editor/player already owns port 8765; refusing to mutate it")

with tempfile.TemporaryDirectory(prefix="native-script-smoke-") as temporary:
    backup = Path(temporary) / game.name
    shutil.copy2(game, backup)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(binary / "PhasmaEditor.exe"), "--api", "vulkan"],
                               cwd=binary, startupinfo=startup)
    try:
        client = wait_for(lambda: connect_mcp("http://127.0.0.1:8765/mcp", 2), 60)
        call_tool(client, "list_editor_actions", {})

        def lua(code):
            result = json.loads(execute_lua(client, code))
            output = result.get("output", "")
            if result.get("error") or output.startswith("error:"):
                raise RuntimeError(result)
            return output.strip()

        def position(name, axis="x"):
            value = lua(f'local n=scene.find_model("{name}"); return n and tostring(n:get_position().{axis}) or "missing"')
            return float(value)

        def status():
            reloads, active, scripts, error = lua(
                'local s=engine.native_scripts_status(); '
                'return string.format("%d|%s|%d|%s", s.reloads, tostring(s.active), s.scripts, s.error)').split("|", 3)
            return {"reloads": int(reloads), "active": active == "true", "scripts": int(scripts), "error": error}

        def near(expression, expected, tolerance=0.01):
            values = [float(v) for v in lua(f'local v={expression}; return string.format("%f,%f,%f", v.x, v.y, v.z)').split(",")]
            return all(abs(a - b) <= tolerance for a, b in zip(values, expected))

        time.sleep(3)  # MCP starts before initial scene loading completes.
        assert lua('engine.set_play_mode(false); scene.add_empty_node("NativeReloadProbe"); '
                   'local n=scene.add_empty_node("NativeNodeProbe"); n:set_script("cpp:NodeProbe", "player"); '
                   'return "created"') == "created"
        shutil.copyfile(probes / "probe1.dll", game)
        wait_for(lambda: position("NativeReloadProbe") == 1)
        assert position("NativeNodeProbe") == 0
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        wait_for(lambda: position("NativeNodeProbe") == 1)
        before = status()
        shutil.copyfile(probes / "probe2.dll", game)
        wait_for(lambda: position("NativeReloadProbe") == 2 and position("NativeNodeProbe") == 2)
        after = status()
        assert after["reloads"] > before["reloads"] and after["error"] == "" and after["active"] and after["scripts"] == 3, after
        # A Lua reload keeps the C++ module and instance state (y counts seconds since instance creation).
        wait_for(lambda: position("NativeNodeProbe", "y") > 3)
        before = position("NativeNodeProbe", "y")
        assert lua('reload_scripts(); return "queued"') == "queued"
        time.sleep(2)
        assert position("NativeNodeProbe") == 2 and position("NativeNodeProbe", "y") > before + 1
        before = status()
        shutil.copyfile(probes / "probe3.dll", game)
        wait_for(lambda: status()["error"] != "")
        assert status()["reloads"] == before["reloads"], status()
        time.sleep(2)
        assert position("NativeReloadProbe") == 2 and position("NativeNodeProbe") == 2
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})
        wait_for(lambda: position("NativeNodeProbe") == 0)
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        wait_for(lambda: position("NativeNodeProbe") == 2)
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})
        wait_for(lambda: position("NativeNodeProbe") == 0)

        # Scene API (added in ABI v4) against real engine state (ApiProbe in module.cpp; x = passed-check bits, y = stage).
        assert lua('local n=scene.add_empty_node("NativeApiProbe"); n:set_script("cpp:ApiProbe", "player"); '
                   'return "created"') == "created"
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        wait_for(lambda: position("NativeApiProbe", "y") == 1)
        assert position("NativeApiProbe") == 127, position("NativeApiProbe")
        assert lua('for _, e in ipairs(scene.get_entities()) do local p=e.node:get_position(); '
                   'if math.abs(p.x-100)<0.01 and math.abs(p.y-200)<0.01 and math.abs(p.z-300)<0.01 then '
                   'api_probe_instance=e.node; return "found" end end; return "missing"') == "found"
        assert near("api_probe_instance:get_rotation()", (0, 45, 0)) and near("api_probe_instance:get_scale()", (2, 3, 4))
        assert near('scene.find_model("NativeApiProbe"):get_rotation()', (0, 30, 0))
        assert near('scene.find_model("NativeApiProbe"):get_scale()', (1, 2, 3))
        assert lua('scene.add_empty_node("ApiProbeDestroy"); return "signalled"') == "signalled"
        wait_for(lambda: position("NativeApiProbe", "y") == 2)
        assert position("NativeApiProbe") == 255, position("NativeApiProbe")
        assert lua('return tostring(api_probe_instance:is_valid())') == "false"
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})

        # A valid module after a rejection reloads and clears the error.
        before = status()
        shutil.copyfile(probes / "probe2.dll", game)
        wait_for(lambda: status()["reloads"] > before["reloads"])
        assert status()["error"] == "", status()

        # An access violation in a script is contained: the editor and this MCP session survive, and the
        # faulted instances are never destroyed (their state may be corrupt) - not on Stop, not on unload.
        log = binary / "PhasmaEngine.log"
        assert log.exists(), f"{log} is required to check fault handling"
        offset = len(log.read_text(errors="replace"))
        before = status()
        shutil.copyfile(probes / "probe7.dll", game)
        wait_for(lambda: status()["reloads"] > before["reloads"])
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        time.sleep(2)
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})
        time.sleep(1)
        assert process.poll() is None and lua('return "alive"') == "alive"
        assert position("NativeReloadProbe") == 2  # probe7 faults before writing
        before = status()
        shutil.copyfile(probes / "probe2.dll", game)  # unloading probe7 must skip the faulted destructors
        wait_for(lambda: status()["reloads"] > before["reloads"])

        def probe7_segment():
            text = log.read_text(errors="replace")[offset:]
            reloads = [i for i in range(len(text)) if text.startswith("[CppScript] module reloaded", i)]
            return text[reloads[0]:reloads[1]] if len(reloads) >= 2 else None  # probe7 load .. probe2 load

        segment = wait_for(probe7_segment, 10)
        assert "[CppScript] fault 0x" in segment, segment
        assert "[CppScript] destroy" not in segment, segment
        print("PASS: editor live replacement, scene retained, Lua reload keeps C++ state, rejected ABI retains code, "
              "node Play/Stop/re-Play, reload status, scene API, fault containment with destroy skipped")
    finally:
        try:
            if "client" in locals():
                execute_lua(client, "engine.quit()")
            process.wait(timeout=5)
        except Exception:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        shutil.copy2(backup, game)
