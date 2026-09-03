"""Drives PhasmaAnimator through its command file (animator_command.json -> animator_result.json beside the exe).
call_tool() speaks the old MCP probe vocabulary so the timeline probes run unchanged."""
import json
import os
import time

EXE_DIR = r"C:\Users\Christos\repos\PhasmaEngine\build-ninja-physics\Release"


class AnimatorClient:
    def __init__(self, exe_dir=EXE_DIR, timeout=60.0):
        self.exe_dir = exe_dir
        self.timeout = timeout
        self.command = os.path.join(exe_dir, "animator_command.json")
        self.result = os.path.join(exe_dir, "animator_result.json")

    def initialize(self):
        deadline = time.time() + 90
        while time.time() < deadline:
            try:
                self.act("animator.state")
                return
            except TimeoutError:
                pass
        raise TimeoutError("PhasmaAnimator did not answer on its command file")

    def send(self, requests):
        for p in (self.result, self.command):
            try:
                os.remove(p)
            except FileNotFoundError:
                pass
        tmp = self.command + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(requests, f)
        os.replace(tmp, self.command)
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            if os.path.exists(self.result):
                try:
                    with open(self.result, "r", encoding="utf-8") as f:
                        results = json.load(f)
                    os.remove(self.result)
                    return results
                except (json.JSONDecodeError, PermissionError):
                    time.sleep(0.02)
                    continue
            time.sleep(0.02)
        raise TimeoutError("no answer for " + json.dumps(requests)[:200])

    def act(self, action, **args):
        return self.send([{"action": action, "args": args}])[0]

    def wait_file(self, path, seconds=8.0):
        deadline = time.time() + seconds
        last = -1
        while time.time() < deadline:
            if os.path.exists(path):
                size = os.path.getsize(path)
                if size > 0 and size == last:
                    return True
                last = size
            time.sleep(0.15)
        return os.path.exists(path)

    # ---- MCP probe compatibility ----
    def call_tool(self, name, args):
        args = dict(args or {})
        if name == "invoke_editor_action":
            action = args.pop("action")
            payload = self.act(action, **args)
        elif name == "load_cooked_mesh":
            payload = self.act("animator.open", path=args.get("path", ""))
        elif name in ("execute_lua", "toggle_editor_window"):
            payload = {"ok": True}
        elif name == "frame_node":
            payload = self.act("timeline.view", frame=True)
        elif name == "take_scene_screenshot":
            # the probes copy the returned file to the path they asked for, so write it beside the exe first
            path = os.path.join(self.exe_dir, "animator_shot.png")
            try:
                os.remove(path)
            except FileNotFoundError:
                pass
            payload = self.act("animator.screenshot", path=path)
            self.wait_file(path)
            payload["path"] = path
        elif name == "get_console_log":
            payload = self.act("animator.log", count=args.get("count", 20), level=args.get("level", ""))
        elif name == "set_camera":
            state = self.act("animator.state")
            payload = {"position": state.get("camera"), "state": state}
        else:
            payload = {"error": "unknown tool for the animator: " + name}
        return {"structuredContent": {"result": payload}}
