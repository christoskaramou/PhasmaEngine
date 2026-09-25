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

        time.sleep(3)  # MCP starts before initial scene loading completes.
        assert lua('engine.set_play_mode(false); scene.add_empty_node("NativeReloadProbe"); '
                   'local n=scene.add_empty_node("NativeNodeProbe"); n:set_script("cpp:NodeProbe", "player"); '
                   'return "created"') == "created"
        shutil.copyfile(probes / "probe1.dll", game)
        wait_for(lambda: position("NativeReloadProbe") == 1)
        assert position("NativeNodeProbe") == 0
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        wait_for(lambda: position("NativeNodeProbe") == 1)
        shutil.copyfile(probes / "probe2.dll", game)
        wait_for(lambda: position("NativeReloadProbe") == 2 and position("NativeNodeProbe") == 2)
        # A Lua reload keeps the C++ module and instance state (y counts seconds since instance creation).
        wait_for(lambda: position("NativeNodeProbe", "y") > 3)
        before = position("NativeNodeProbe", "y")
        assert lua('reload_scripts(); return "queued"') == "queued"
        time.sleep(2)
        assert position("NativeNodeProbe") == 2 and position("NativeNodeProbe", "y") > before + 1
        shutil.copyfile(probes / "probe3.dll", game)
        time.sleep(2)
        assert position("NativeReloadProbe") == 2 and position("NativeNodeProbe") == 2
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})
        wait_for(lambda: position("NativeNodeProbe") == 0)
        call_tool(client, "invoke_editor_action", {"action": "play.start"})
        wait_for(lambda: position("NativeNodeProbe") == 2)
        call_tool(client, "invoke_editor_action", {"action": "play.stop"})
        wait_for(lambda: position("NativeNodeProbe") == 0)
        print("PASS: editor live replacement, scene retained, Lua reload keeps C++ state, rejected ABI retains code, node Play/Stop/re-Play")
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
