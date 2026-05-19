#!/usr/bin/env python3
"""PhasmaEngine pre-commit validation orchestrator.

This script is intentionally conservative: it wraps existing repo commands and
editor MCP tools instead of inventing a second build/test system.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zlib
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
SIDECAR_DIR = TOOLS / "phasma_adk_agent"
DEFAULT_BUILD_DIRS = (ROOT / "build-ninja-full", ROOT / "build")
DEFAULT_CONFIG = "Release"
DEFAULT_SCENE_ASSET_PATH = "Assets/Scenes/sponza.pescene"
DEFAULT_SCENE_LUA_PATH = "Scenes/sponza.pescene"
DEFAULT_SCENE = DEFAULT_SCENE_ASSET_PATH
REPORT_ROOT = ROOT / "reports" / "precommit_validation"
INSTRUCTION_SYNC_FILES = (
    ROOT / "AGENTS.md",
    ROOT / "CLAUDE.md",
    ROOT / "GEMINI.md",
    ROOT / ".github" / "copilot-instructions.md",
)
ADK_READ_ONLY_TOOLS = {"query_scene", "take_screenshot", "get_console_log"}
MUTATING_TOOLS = {"write_project_file", "patch_project_file", "execute_lua", "inject_mouse_input"}
MODEL_API_KEY_ENV_VARS = ("OPENAI_API_KEY", "GOOGLE_API_KEY", "GEMINI_API_KEY")

FAIL_PATTERNS = [
    re.compile(pattern, re.IGNORECASE)
    for pattern in [
        r"\bPE_ERROR\b",
        r"^\s*(?:\[[^\]]+\]\s*)?(?:ERROR|FATAL)(?:\s*[:\]]|\s+-|\s+\[|$)",
        r"\[(?:ERROR|FATAL)\]",
        r"\b(?:ERROR|FATAL)(?:\s*[:\]])",
        r"Validation Error",
        r"\bVUID-",
        r"\bVK_ERROR",
        r"\bD3D12 ERROR",
        r"\bDXGI_ERROR",
        r"device removed",
        r"GPU[- ]Based Validation.*error",
        r"assertion failed",
        r"unhandled exception",
    ]
]

LOG_ALLOW_PATTERNS = [
    re.compile(pattern, re.IGNORECASE)
    for pattern in [
        r"0 errors",
        r"\berror_count\s*[:=]\s*0\b",
        r"\bERROR_NONE\b",
        r"No recent error",
        r"Vulkan validation layer enabled",
        r"PE_VULKAN_VALIDATION",
        r"validation mode",
    ]
]


@dataclass
class Step:
    name: str
    status: str
    detail: str = ""
    seconds: float = 0.0


class Validator:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.report_dir = args.report_dir or (REPORT_ROOT / timestamp)
        self.report_dir.mkdir(parents=True, exist_ok=True)
        self.steps: list[Step] = []
        self.mcp_client = None
        self.mcp_report: dict | None = None
        self.processes: list[tuple[str, subprocess.Popen[str]]] = []
        self.editor_mcp_bin_dir: Path | None = None
        self.editor_mcp_reused = False

    def add(self, name: str, status: str, detail: str = "", seconds: float = 0.0) -> None:
        self.steps.append(Step(name=name, status=status, detail=detail, seconds=seconds))
        mark = {"PASS": "[PASS]", "FAIL": "[FAIL]", "WARN": "[WARN]", "SKIP": "[SKIP]"}[status]
        suffix = f" ({seconds:.1f}s)" if seconds else ""
        print(f"{mark} {name}{suffix}")
        if detail:
            print(indent(detail.rstrip(), "       "))

    def run_cmd(
        self,
        name: str,
        cmd: list[str],
        *,
        cwd: Path = ROOT,
        timeout: int | None = None,
        env: dict[str, str] | None = None,
        allow_fail: bool = False,
        log_name: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        start = time.monotonic()
        try:
            completed = subprocess.run(
                cmd,
                cwd=str(cwd),
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
        except FileNotFoundError as exc:
            self.add(name, "FAIL", f"Command not found: {cmd[0]}", time.monotonic() - start)
            raise RuntimeError(name) from exc
        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            self._write_log(log_name or name, output)
            self.add(name, "FAIL", f"Timed out after {timeout}s", time.monotonic() - start)
            raise RuntimeError(name) from exc

        self._write_log(log_name or name, completed.stdout)
        status = "PASS" if completed.returncode == 0 else ("WARN" if allow_fail else "FAIL")
        detail = "" if completed.returncode == 0 else tail(completed.stdout, 40)
        self.add(name, status, detail, time.monotonic() - start)
        if completed.returncode != 0 and not allow_fail:
            raise RuntimeError(name)
        return completed

    def _write_log(self, name: str, content: str) -> Path:
        safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("_")
        path = self.report_dir / f"{safe}.log"
        path.write_text(content or "", encoding="utf-8", errors="replace")
        return path

    def finish(self) -> int:
        self.cleanup_processes()
        summary = {
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "report_dir": str(self.report_dir),
            "steps": [step.__dict__ for step in self.steps],
        }
        (self.report_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

        failed = [step for step in self.steps if step.status == "FAIL"]
        warned = [step for step in self.steps if step.status == "WARN"]
        print("\n=== Summary ===")
        print(f"Report: {self.report_dir}")
        print(f"Passed: {sum(1 for step in self.steps if step.status == 'PASS')}")
        print(f"Warnings: {len(warned)}")
        print(f"Failures: {len(failed)}")
        return 1 if failed else 0

    def track_process(self, label: str, process: subprocess.Popen[str]) -> None:
        self.processes.append((label, process))

    def cleanup_processes(self, label_prefix: str | None = None) -> None:
        remaining: list[tuple[str, subprocess.Popen[str]]] = []
        for label, process in self.processes:
            if label_prefix and not label.startswith(label_prefix):
                remaining.append((label, process))
                continue
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        pass
        self.processes = remaining


def indent(text: str, prefix: str) -> str:
    return "\n".join(prefix + line for line in text.splitlines())


def tail(text: str, lines: int) -> str:
    split = (text or "").splitlines()
    return "\n".join(split[-lines:])


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def scene_lua_path(scene: str) -> str:
    normalized = scene.replace("\\", "/")
    if normalized == DEFAULT_SCENE_ASSET_PATH:
        return DEFAULT_SCENE_LUA_PATH
    asset_prefix = "Assets/"
    if normalized.startswith(asset_prefix):
        return normalized[len(asset_prefix):]
    return normalized


def choose_build_dir(explicit: Path | None) -> Path:
    if explicit:
        return explicit if explicit.is_absolute() else ROOT / explicit
    for path in DEFAULT_BUILD_DIRS:
        if path.exists():
            return path
    return DEFAULT_BUILD_DIRS[0]


def build_bin_dir(build_dir: Path, config: str) -> Path:
    return build_dir / config


def python_executable() -> str:
    return sys.executable or "python"


def git_status_step(v: Validator) -> None:
    completed = v.run_cmd("git status", ["git", "status", "--short", "--untracked-files=all"], log_name="git_status")
    status = completed.stdout.strip()
    third_party_lines = [line for line in status.splitlines() if "third_party/" in line or "third_party\\" in line]
    if third_party_lines:
        v.add("third_party guard", "FAIL", "\n".join(third_party_lines))
    else:
        v.add("third_party guard", "PASS")


def hygiene_steps(v: Validator) -> None:
    v.run_cmd("git diff whitespace", ["git", "diff", "--check"], log_name="git_diff_check")
    instruction_sync_step(v)
    adk_read_only_guard_step(v)
    clang_format_dry_run_step(v)

    files = [
        TOOLS / "editor_stress.py",
        TOOLS / "precommit_validate.py",
        SIDECAR_DIR / "agent.py",
        SIDECAR_DIR / "mcp_probe.py",
    ]
    code = "\n".join(
        [
            "from pathlib import Path",
            "files = " + repr([str(path) for path in files if path.exists()]),
            "for path in files:",
            "    compile(Path(path).read_text(encoding='utf-8'), path, 'exec')",
            "print('python syntax ok')",
        ]
    )
    v.run_cmd("python syntax", [python_executable(), "-c", code], log_name="python_syntax")

    wiki_lint = ROOT / "docs" / "wiki" / "tools" / "lint.sh"
    if wiki_lint.exists():
        if shutil.which("bash"):
            v.run_cmd("wiki lint", ["bash", "docs/wiki/tools/lint.sh"], log_name="wiki_lint")
        else:
            python_wiki_lint(v)
    else:
        v.add("wiki lint", "SKIP", "docs/wiki/tools/lint.sh not found")


def instruction_sync_step(v: Validator) -> None:
    start = time.monotonic()
    missing = [rel(path) for path in INSTRUCTION_SYNC_FILES if not path.exists()]
    if missing:
        v.add("instruction sync", "FAIL", "missing:\n" + "\n".join(missing), time.monotonic() - start)
        raise RuntimeError("instruction sync")

    contents = {
        path: path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
        for path in INSTRUCTION_SYNC_FILES
    }
    base_path = INSTRUCTION_SYNC_FILES[0]
    base = contents[base_path]
    drift = [path for path, text in contents.items() if text != base]
    if drift:
        detail = "Instruction files must match AGENTS.md:\n" + "\n".join(rel(path) for path in drift)
        v.add("instruction sync", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError("instruction sync")

    digest = hashlib.sha256(base.encode("utf-8")).hexdigest()[:12]
    v.add("instruction sync", "PASS", f"{len(INSTRUCTION_SYNC_FILES)} files match; sha256={digest}", time.monotonic() - start)


def adk_read_only_guard_step(v: Validator) -> None:
    start = time.monotonic()
    agent_path = SIDECAR_DIR / "agent.py"
    if not agent_path.exists():
        v.add("adk read-only guard", "FAIL", f"Missing {rel(agent_path)}", time.monotonic() - start)
        raise RuntimeError("adk read-only guard")

    tree = ast.parse(agent_path.read_text(encoding="utf-8"), filename=str(agent_path))
    tool_names: set[str] | None = None
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(
            isinstance(target, ast.Name) and target.id == "READ_ONLY_MCP_TOOLS"
            for target in node.targets
        ):
            continue
        value = ast.literal_eval(node.value)
        tool_names = set(value)
        break

    if tool_names is None:
        v.add("adk read-only guard", "FAIL", "READ_ONLY_MCP_TOOLS not found", time.monotonic() - start)
        raise RuntimeError("adk read-only guard")

    extra = sorted(tool_names - ADK_READ_ONLY_TOOLS)
    missing = sorted(ADK_READ_ONLY_TOOLS - tool_names)
    mutating = sorted(tool_names & MUTATING_TOOLS)
    if extra or missing or mutating:
        detail = []
        if missing:
            detail.append("missing: " + ", ".join(missing))
        if extra:
            detail.append("unexpected: " + ", ".join(extra))
        if mutating:
            detail.append("mutating tools exposed: " + ", ".join(mutating))
        v.add("adk read-only guard", "FAIL", "\n".join(detail), time.monotonic() - start)
        raise RuntimeError("adk read-only guard")

    v.add("adk read-only guard", "PASS", ", ".join(sorted(tool_names)), time.monotonic() - start)


def changed_cpp_headers() -> list[Path]:
    completed = subprocess.run(
        ["git", "status", "--short", "--untracked-files=all"],
        cwd=str(ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    paths: list[Path] = []
    for line in completed.stdout.splitlines():
        if not line:
            continue
        path_text = line[3:] if len(line) > 3 else ""
        if " -> " in path_text:
            path_text = path_text.split(" -> ", 1)[1]
        path = (ROOT / path_text.strip()).resolve()
        suffix = path.suffix.lower()
        path_norm = path.as_posix().lower()
        if suffix in {".cpp", ".h", ".hpp", ".cxx", ".cc"} and "/third_party/" not in path_norm:
            paths.append(path)
    return sorted(set(paths))


def find_clang_format_binary() -> str | None:
    for name in ("clang-format", "clang-format.exe"):
        found = shutil.which(name)
        if found:
            return found

    for candidate in (
        Path("/mnt/c/Program Files/LLVM/bin/clang-format.exe"),
        Path("/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"),
        Path("/mnt/c/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin/clang-format.exe"),
        Path("/mnt/c/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin/clang-format.exe"),
    ):
        if candidate.exists():
            return str(candidate)
    return None


def find_adk_binary() -> str | None:
    for name in ("adk", "adk.exe"):
        found = shutil.which(name)
        if found:
            return found

    for candidate in (
        TOOLS / ".venv" / "Scripts" / "adk.exe",
        TOOLS / ".venv" / "bin" / "adk",
        SIDECAR_DIR / ".venv" / "Scripts" / "adk.exe",
        SIDECAR_DIR / ".venv" / "bin" / "adk",
    ):
        if candidate.exists():
            return str(candidate)
    return None


def read_dotenv_file(path: Path, env: dict[str, str]) -> None:
    if not path.exists():
        return
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if not key or key in env:
            continue
        value = value.strip().strip("'\"")
        env[key] = value


def adk_subprocess_env() -> dict[str, str]:
    env = os.environ.copy()
    for path in (ROOT / ".env", TOOLS / ".env", SIDECAR_DIR / ".env"):
        read_dotenv_file(path, env)
    return env


def has_model_api_key(env: dict[str, str]) -> bool:
    return any(env.get(name) for name in MODEL_API_KEY_ENV_VARS)


def clang_format_dry_run_step(v: Validator) -> None:
    start = time.monotonic()
    paths = changed_cpp_headers()
    if not paths:
        v.add("clang-format", "PASS", "No modified C/C++ files", time.monotonic() - start)
        return
    clang_format = find_clang_format_binary()
    if not clang_format:
        v.add("clang-format", "FAIL", "clang-format not found", time.monotonic() - start)
        raise RuntimeError("clang-format")

    outputs: list[str] = []
    unformatted: list[Path] = []
    for path in paths:
        display_path = rel(path)
        cmd = [clang_format, "--output-replacements-xml", display_path]
        try:
            completed = subprocess.run(
                cmd,
                cwd=str(ROOT),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=120,
            )
        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            v._write_log("clang_format", output)
            v.add("clang-format", "FAIL", f"Timed out after 120s while checking {display_path}", time.monotonic() - start)
            raise RuntimeError("clang-format") from exc

        outputs.append(f"$ {' '.join(cmd)}\n{completed.stdout}")
        if completed.returncode != 0:
            v._write_log("clang_format", "\n\n".join(outputs))
            v.add("clang-format", "FAIL", tail(completed.stdout, 40), time.monotonic() - start)
            raise RuntimeError("clang-format")

        try:
            root = ET.fromstring(completed.stdout)
        except ET.ParseError as exc:
            v._write_log("clang_format", "\n\n".join(outputs))
            v.add("clang-format", "FAIL", f"Invalid clang-format XML for {display_path}", time.monotonic() - start)
            raise RuntimeError("clang-format") from exc
        if root.findall("replacement"):
            unformatted.append(path)

    v._write_log("clang_format", "\n\n".join(outputs))
    if unformatted:
        detail = "Run clang-format -i on:\n" + "\n".join(rel(path) for path in unformatted)
        v.add("clang-format", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError("clang-format")

    v.add("clang-format", "PASS", f"checked {len(paths)} file(s)", time.monotonic() - start)


def python_wiki_lint(v: Validator) -> None:
    start = time.monotonic()
    wiki = ROOT / "docs" / "wiki"
    required = [
        wiki / "README.md",
        wiki / "index.md",
        wiki / "log.md",
        wiki / "tools" / "search.sh",
        wiki / "tools" / "lint.sh",
    ]
    missing = [rel(path) for path in required if not path.exists()]
    tabbed: list[str] = []
    for path in wiki.rglob("*.md"):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "\t" in text:
            tabbed.append(rel(path))

    if missing or tabbed:
        detail = ""
        if missing:
            detail += "missing:\n" + "\n".join(missing)
        if tabbed:
            detail += ("\n" if detail else "") + "tabs:\n" + "\n".join(tabbed)
        v.add("wiki lint", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError("wiki lint")
    v.add("wiki lint", "PASS", "python fallback", time.monotonic() - start)


def build_step(v: Validator, build_dir: Path) -> None:
    if not build_dir.exists():
        v.add(
            "build",
            "FAIL",
            f"Build directory not found: {build_dir}\nConfigure first, for example: cmake --preset ninja-full",
        )
        raise RuntimeError("build")

    cmd = ["cmake", "--build", str(build_dir), "--config", v.args.config]
    if v.args.targets:
        cmd += ["--target", *v.args.targets]
    v.run_cmd("build", cmd, timeout=v.args.build_timeout, log_name="build")


def launcher_smoke_step(v: Validator, build_dir: Path) -> None:
    if not v.args.launcher_smoke:
        v.add("launcher smoke", "SKIP", "disabled")
        return
    bin_dir = build_bin_dir(build_dir, v.args.config)
    exe = bin_dir / "PhasmaLauncher.exe"
    if not exe.exists():
        v.add("launcher smoke", "FAIL", f"Missing executable: {exe}")
        raise RuntimeError("launcher smoke")

    clear_engine_log(bin_dir)
    start = time.monotonic()
    stdout_path = v.report_dir / "launcher_smoke_stdout.log"
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        proc = subprocess.Popen(
            [str(exe), "--api", v.args.editor_api],
            cwd=str(bin_dir),
            env=os.environ.copy(),
            stdout=stdout,
            stderr=subprocess.STDOUT,
            text=True,
        )

    time.sleep(v.args.launcher_smoke_seconds)
    exited = proc.poll()
    log_text = read_engine_log(bin_dir)
    log_detail = scan_engine_log(v, bin_dir, "launcher")
    if exited is not None and exited != 0:
        detail = f"Exited early with code 0x{exited & 0xFFFFFFFF:08X}\n" + log_detail
        v.add("launcher smoke", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError("launcher smoke")

    if exited is None:
        proc.kill()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass

    bad_lines = find_bad_log_lines(log_text)
    if bad_lines:
        v.add("launcher smoke", "FAIL", "Validation/error lines:\n" + "\n".join(bad_lines[:40]), time.monotonic() - start)
        raise RuntimeError("launcher smoke")

    status_detail = "alive during smoke; killed by validator" if exited is None else "exited cleanly"
    v.add("launcher smoke", "PASS", status_detail, time.monotonic() - start)


def import_mcp_client():
    sys.path.insert(0, str(TOOLS))
    from phasma_adk_agent.mcp_probe import McpClient, build_report

    return McpClient, build_report


def probe_mcp_once(v: Validator, timeout: float | None = None) -> tuple[object | None, dict | None, Exception | None]:
    McpClient, build_report = import_mcp_client()
    client = McpClient(v.args.mcp_url, timeout or v.args.mcp_timeout)
    try:
        report = build_report(client, run_smoke=True, include_screenshot=False)
    except Exception as exc:
        return None, None, exc
    return client, report, None


def ensure_agent_config_mcp(bin_dir: Path) -> Path:
    path = bin_dir / "Assets" / "Agent" / "agent_config.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    data: dict = {}
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


def ensure_startup_files(bin_dir: Path, scene: str) -> None:
    assets = bin_dir / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    (assets / "editor_config.json").write_text(json.dumps({"last_scene": scene}, indent=2), encoding="utf-8")
    (bin_dir / "phasma_settings.json").write_text(json.dumps({"startup_scene": scene}, indent=2), encoding="utf-8")


def launch_editor_for_mcp(v: Validator, build_dir: Path) -> None:
    if not v.args.launch_editor:
        v.add("editor launch", "SKIP", "--no-launch-editor active")
        return

    client, report, _ = probe_mcp_once(v, timeout=1.0)
    if report:
        v.mcp_client = client
        v.mcp_report = report
        v.editor_mcp_reused = True
        v.add("editor launch", "PASS", "MCP already reachable; reusing running editor")
        return

    bin_dir = build_bin_dir(build_dir, v.args.config)
    v.editor_mcp_bin_dir = bin_dir
    v.editor_mcp_reused = False
    exe = bin_dir / "PhasmaEditor.exe"
    if not exe.exists():
        v.add("editor launch", "FAIL", f"Missing executable: {exe}")
        raise RuntimeError("editor launch")

    ensure_startup_files(bin_dir, v.args.scene)
    config_path = ensure_agent_config_mcp(bin_dir)
    remove_hot_reload_modules(bin_dir)
    clear_engine_log(bin_dir)

    stdout_path = v.report_dir / "editor_mcp_stdout.log"
    start = time.monotonic()
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        process = subprocess.Popen(
            [str(exe), "--api", v.args.editor_api, "--display", str(v.args.display)],
            cwd=str(bin_dir),
            env=os.environ.copy(),
            stdout=stdout,
            stderr=subprocess.STDOUT,
            text=True,
        )
    v.track_process("editor-mcp", process)

    deadline = time.monotonic() + v.args.editor_start_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            detail = f"Exited early with code 0x{process.returncode & 0xFFFFFFFF:08X}"
            detail += "\n" + scan_engine_log(v, bin_dir, "editor_mcp")
            v.add("editor launch", "FAIL", detail, time.monotonic() - start)
            raise RuntimeError("editor launch")
        client, report, error = probe_mcp_once(v, timeout=1.0)
        if report:
            v.mcp_client = client
            v.mcp_report = report
            detail = (
                f"pid={process.pid}\napi={v.args.editor_api}\ndisplay={v.args.display}\n"
                f"agent_config={config_path}"
            )
            v.add("editor launch", "PASS", detail, time.monotonic() - start)
            return
        last_error = error
        time.sleep(1)

    detail = f"Timed out waiting for MCP at {v.args.mcp_url}"
    if last_error:
        detail += f"\nlast error: {last_error}"
    detail += "\n" + scan_engine_log(v, bin_dir, "editor_mcp")
    v.add("editor launch", "FAIL", detail, time.monotonic() - start)
    raise RuntimeError("editor launch")


def mcp_probe_step(v: Validator) -> None:
    start = time.monotonic()
    exc: Exception | None = None
    if v.mcp_report is not None:
        report = v.mcp_report
    else:
        client, report, exc = probe_mcp_once(v)
        if report is not None:
            v.mcp_client = client
            v.mcp_report = report
    if not report:
        v.add(
            "mcp probe",
            "FAIL",
            f"{exc}\nStart PhasmaEditor and enable Connection -> MCP Server.",
            time.monotonic() - start,
        )
        if v.args.require_mcp:
            raise RuntimeError("mcp probe") from exc
        return

    (v.report_dir / "mcp_probe.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    details = [
        f"server={report.get('server', {}).get('name', 'unknown')}",
        f"protocol={report.get('protocol_version')}",
        f"tools={report.get('tool_count')}",
    ]
    if report.get("visible_mutating_tools"):
        details.append("mutating tools visible on server; ADK sidecar still filters them")
    missing = report.get("missing_phase1_tools") or []
    schema_missing = []
    smoke = report.get("smoke") or {}
    for tool_name, required_keys in {
        "query_scene": {"total_nodes", "nodes", "cameras"},
        "get_console_log": {"total", "entries"},
    }.items():
        keys = set((smoke.get(tool_name) or {}).get("structured_keys") or [])
        missing_keys = sorted(required_keys - keys)
        if missing_keys:
            schema_missing.append(f"{tool_name}: " + ", ".join(missing_keys))
    status = "FAIL" if missing else "PASS"
    if missing:
        details.append("missing Phase 1 tools: " + ", ".join(missing))
    if schema_missing:
        status = "FAIL"
        details.append("missing structured result keys: " + "; ".join(schema_missing))
    v.add("mcp probe", status, "\n".join(details), time.monotonic() - start)
    if missing or schema_missing:
        raise RuntimeError("mcp probe")


def editor_display_assertion_step(v: Validator) -> None:
    start = time.monotonic()
    if v.editor_mcp_reused:
        v.add("editor display", "SKIP", "MCP already reachable; existing editor display not controlled by validator")
        return
    if not v.editor_mcp_bin_dir:
        v.add("editor display", "SKIP", "Editor launch did not record a build output directory")
        return
    log_text = read_engine_log(v.editor_mcp_bin_dir)
    if display_marker_present(log_text, v.args.display):
        v.add("editor display", "PASS", f"display={v.args.display}", time.monotonic() - start)
        return
    detail = f"Missing log marker for display {v.args.display}\n" + tail(log_text, 40)
    v.add("editor display", "FAIL", detail, time.monotonic() - start)
    raise RuntimeError("editor display")


def mcp_internal_smoke_step(v: Validator) -> None:
    if not v.args.mcp_internal_smoke:
        v.add("mcp internal smoke", "SKIP", "disabled")
        return
    if not v.mcp_client:
        v.add("mcp internal smoke", "SKIP", "MCP client unavailable")
        return
    start = time.monotonic()
    code = 'pe_log("[precommit:mcp] execute_lua round-trip ok")\nreturn "PRECOMMIT_LUA_OK"'
    try:
        result = call_tool_with_timeout(v, "execute_lua", {"code": code}, v.args.mcp_tool_timeout)
    except Exception as exc:
        v.add("mcp internal smoke", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("mcp internal smoke") from exc
    (v.report_dir / "mcp_execute_lua.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if result.get("isError"):
        v.add("mcp internal smoke", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
        raise RuntimeError("mcp internal smoke")
    v.add("mcp internal smoke", "PASS", "execute_lua round-trip ok", time.monotonic() - start)


def console_log_scan_step(v: Validator, label: str) -> None:
    if not v.args.console_scan:
        v.add(f"console scan {label}", "SKIP", "disabled")
        return
    if not v.mcp_client:
        v.add(f"console scan {label}", "SKIP", "MCP client unavailable")
        return
    start = time.monotonic()
    try:
        result = call_tool_with_timeout(
            v, "get_console_log", {"count": v.args.console_scan_count, "level": "error"}, v.args.mcp_tool_timeout
        )
    except Exception as exc:
        v.add(f"console scan {label}", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError(f"console scan {label}") from exc
    (v.report_dir / f"console_errors_{label}.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if result.get("isError"):
        v.add(f"console scan {label}", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
        raise RuntimeError(f"console scan {label}")

    data = result.get("structuredContent") or {}
    entries = data.get("entries") or []
    if entries:
        lines = []
        for entry in entries[:20]:
            if isinstance(entry, dict):
                message = entry.get("message") or entry.get("text") or json.dumps(entry)
                lines.append(str(message))
            else:
                lines.append(str(entry))
        v.add(f"console scan {label}", "FAIL", "\n".join(lines), time.monotonic() - start)
        raise RuntimeError(f"console scan {label}")
    v.add(f"console scan {label}", "PASS", "No console errors", time.monotonic() - start)


def call_tool_with_timeout(v: Validator, name: str, args: dict | None = None, timeout: float | None = None) -> dict:
    if not v.mcp_client:
        raise RuntimeError("MCP client unavailable")
    original_timeout = getattr(v.mcp_client, "timeout", None)
    if timeout is not None and original_timeout is not None:
        v.mcp_client.timeout = max(original_timeout, timeout)
    try:
        return v.mcp_client.call_tool(name, args)
    finally:
        if original_timeout is not None:
            v.mcp_client.timeout = original_timeout


def editor_play_lifecycle_step(v: Validator) -> None:
    if not v.args.play_lifecycle:
        v.add("play lifecycle", "SKIP", "disabled")
        return
    if not v.mcp_client:
        v.add("play lifecycle", "SKIP", "MCP client unavailable")
        return
    start = time.monotonic()
    results = {}
    sequence = [
        ("play.start", {}),
        ("play.pause", {"open": True}),
        ("play.pause", {"open": False}),
        ("play.stop", {}),
    ]
    for action, extra in sequence:
        args = {"action": action}
        args.update(extra)
        try:
            result = call_tool_with_timeout(v, "invoke_editor_action", args, v.args.play_lifecycle_timeout)
        except Exception as exc:
            (v.report_dir / "play_lifecycle.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            v.add("play lifecycle", "FAIL", f"{action} timed out or failed: {exc}", time.monotonic() - start)
            raise RuntimeError("play lifecycle") from exc
        results[action + str(extra)] = result
        if result.get("isError"):
            (v.report_dir / "play_lifecycle.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            v.add("play lifecycle", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
            raise RuntimeError("play lifecycle")
        time.sleep(v.args.play_lifecycle_pause)

    try:
        scene = call_tool_with_timeout(v, "query_scene", None, v.args.play_lifecycle_timeout)
    except Exception as exc:
        (v.report_dir / "play_lifecycle.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        v.add("play lifecycle", "FAIL", f"query_scene timed out or failed: {exc}", time.monotonic() - start)
        raise RuntimeError("play lifecycle") from exc
    results["query_scene"] = scene
    (v.report_dir / "play_lifecycle.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    if scene.get("isError"):
        v.add("play lifecycle", "FAIL", json.dumps(scene, indent=2), time.monotonic() - start)
        raise RuntimeError("play lifecycle")
    v.add("play lifecycle", "PASS", "start -> pause -> resume -> stop", time.monotonic() - start)


def hot_reload_smoke_step(v: Validator) -> None:
    if not v.args.hot_reload_smoke:
        v.add("hot reload smoke", "SKIP", "disabled")
        return
    if not v.mcp_client:
        v.add("hot reload smoke", "SKIP", "MCP client unavailable")
        return
    start = time.monotonic()
    try:
        result = call_tool_with_timeout(v, "reload_module", None, v.args.hot_reload_timeout)
    except Exception as exc:
        v.add("hot reload smoke", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("hot reload smoke") from exc
    (v.report_dir / "hot_reload_request.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if result.get("isError"):
        v.add("hot reload smoke", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
        raise RuntimeError("hot reload smoke")

    deadline = time.monotonic() + v.args.hot_reload_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        client, report, error = probe_mcp_once(v, timeout=1.0)
        if report:
            v.mcp_client = client
            v.mcp_report = report
            scene = call_tool_with_timeout(v, "query_scene", None, v.args.mcp_tool_timeout)
            if not scene.get("isError"):
                v.add("hot reload smoke", "PASS", "MCP reachable after reload", time.monotonic() - start)
                return
        last_error = error
        time.sleep(1)
    detail = f"Timed out waiting for MCP after reload"
    if last_error:
        detail += f"\nlast error: {last_error}"
    v.add("hot reload smoke", "FAIL", detail, time.monotonic() - start)
    raise RuntimeError("hot reload smoke")


def script_tests_step(v: Validator) -> None:
    if not v.args.script_tests:
        v.add("script tests", "SKIP", "disabled")
        return
    if not v.mcp_client:
        v.add("script tests", "SKIP", "MCP client unavailable")
        return
    start = time.monotonic()
    try:
        result = call_tool_with_timeout(
            v, "invoke_editor_action", {"action": "help.run_script_tests_all"}, v.args.mcp_tool_timeout
        )
    except Exception as exc:
        v.add("script tests", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("script tests") from exc
    (v.report_dir / "script_tests_request.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if result.get("isError"):
        v.add("script tests", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
        raise RuntimeError("script tests")
    time.sleep(v.args.script_tests_wait)
    try:
        log_result = call_tool_with_timeout(
            v, "get_console_log", {"count": v.args.console_scan_count, "level": "all"}, v.args.mcp_tool_timeout
        )
    except Exception as exc:
        v.add("script tests", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("script tests") from exc
    (v.report_dir / "script_tests_console.json").write_text(json.dumps(log_result, indent=2), encoding="utf-8")
    text = json.dumps(log_result)
    failed = re.search(r"TEST RESULTS:\s*\d+\s+passed,\s*([1-9]\d*)\s+failed", text)
    if failed:
        v.add("script tests", "FAIL", "Script test failures reported", time.monotonic() - start)
        raise RuntimeError("script tests")
    if "TEST RESULTS:" not in text:
        v.add("script tests", "WARN", "Script test summary not found in recent console log", time.monotonic() - start)
        return
    v.add("script tests", "PASS", "Script test summary reports zero failures", time.monotonic() - start)


def adk_smoke_step(v: Validator) -> None:
    adk = find_adk_binary()
    if not adk:
        status = "FAIL" if v.args.require_adk else "SKIP"
        v.add("adk smoke", status, "adk not found on PATH or in tools/.venv")
        if v.args.require_adk:
            raise RuntimeError("adk smoke")
        return
    env = adk_subprocess_env()
    if not has_model_api_key(env):
        status = "FAIL" if v.args.require_adk else "SKIP"
        v.add("adk smoke", status, "No model API key env var found in process env or local .env files")
        if v.args.require_adk:
            raise RuntimeError("adk smoke")
        return

    prompt = (
        "Use query_scene and get_console_log. If both tools return concrete data, "
        "reply with a line starting ADK_MCP_OK and include node/camera/light counts. "
        "If either tool is unavailable or empty, reply ADK_MCP_FAIL with the reason.\n"
        "exit\n"
    )
    start = time.monotonic()
    try:
        completed = subprocess.run(
            [adk, "run", "phasma_adk_agent"],
            cwd=str(TOOLS),
            env=env,
            input=prompt,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=v.args.adk_timeout,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        v._write_log("adk_smoke", output)
        v.add("adk smoke", "FAIL", f"Timed out after {v.args.adk_timeout}s", time.monotonic() - start)
        if v.args.require_adk:
            raise RuntimeError("adk smoke") from exc
        return

    v._write_log("adk_smoke", completed.stdout)
    if completed.returncode == 0 and "ADK_MCP_OK" in completed.stdout:
        v.add("adk smoke", "PASS", "ADK_MCP_OK observed", time.monotonic() - start)
        return

    status = "FAIL" if v.args.require_adk else "WARN"
    v.add("adk smoke", status, tail(completed.stdout, 60), time.monotonic() - start)
    if status == "FAIL":
        raise RuntimeError("adk smoke")


def ensure_scene_config(bin_dir: Path, scene: str) -> None:
    ensure_startup_files(bin_dir, scene)
    ensure_agent_config_mcp(bin_dir)


def remove_hot_reload_modules(bin_dir: Path) -> None:
    for path in bin_dir.glob("PhasmaEditorModule_*.dll"):
        try:
            path.unlink()
        except OSError:
            pass


def smoke_backend(v: Validator, build_dir: Path, api: str) -> None:
    bin_dir = build_bin_dir(build_dir, v.args.config)
    exe = bin_dir / "PhasmaEditor.exe"
    if not exe.exists():
        v.add(f"{api} smoke", "FAIL", f"Missing executable: {exe}")
        raise RuntimeError(f"{api} smoke")

    ensure_scene_config(bin_dir, v.args.scene)
    remove_hot_reload_modules(bin_dir)
    clear_engine_log(bin_dir)

    env = os.environ.copy()
    if v.args.validation:
        if api == "vulkan":
            env["PE_VULKAN_VALIDATION"] = "1"
        elif api == "dx12":
            env["PE_DX12_DEBUG"] = "1"
            env["PE_DX12_GBV"] = "1"
            env["PE_DX12_DRED"] = "1"

    start = time.monotonic()
    stdout_path = v.report_dir / f"{api}_smoke_stdout.log"
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        proc = subprocess.Popen(
            [str(exe), "--api", api, "--display", str(v.args.display)],
            cwd=str(bin_dir),
            env=env,
            stdout=stdout,
            stderr=subprocess.STDOUT,
        )

    time.sleep(v.args.smoke_seconds)
    exited = proc.poll()
    if exited is not None:
        detail = f"Exited early with code 0x{exited & 0xFFFFFFFF:08X}"
        detail += "\n" + scan_engine_log(v, bin_dir, api)
        v.add(f"{api} smoke", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError(f"{api} smoke")

    proc.kill()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass

    log_text = read_engine_log(bin_dir)
    log_detail = scan_engine_log(v, bin_dir, api)
    if not display_marker_present(log_text, v.args.display):
        v.add(f"{api} smoke", "FAIL", f"Display {v.args.display} marker missing\n" + log_detail, time.monotonic() - start)
        raise RuntimeError(f"{api} smoke")

    if not scene_load_marker_present(log_text, v.args.scene):
        v.add(f"{api} smoke", "FAIL", f"Scene load marker missing for {v.args.scene}\n" + log_detail, time.monotonic() - start)
        raise RuntimeError(f"{api} smoke")

    bad_lines = find_bad_log_lines(log_text)
    if bad_lines:
        v.add(f"{api} smoke", "FAIL", "Validation/error lines:\n" + "\n".join(bad_lines[:40]), time.monotonic() - start)
        raise RuntimeError(f"{api} smoke")

    v.add(f"{api} smoke", "PASS", tail(log_detail, 12), time.monotonic() - start)


def scan_engine_log(v: Validator, bin_dir: Path, api: str) -> str:
    text = read_engine_log(bin_dir)
    if text.startswith("Missing log:"):
        return text
    log_tail = tail(text, v.args.log_lines)
    (v.report_dir / f"{api}_PhasmaEngine_tail.log").write_text(log_tail, encoding="utf-8", errors="replace")
    return log_tail


def read_engine_log(bin_dir: Path) -> str:
    log = bin_dir / "PhasmaEngine.log"
    if not log.exists():
        return f"Missing log: {log}"
    return log.read_text(encoding="utf-8", errors="replace")


def find_bad_log_lines(log_text: str) -> list[str]:
    bad: list[str] = []
    for line_no, line in enumerate(log_text.splitlines(), start=1):
        # Allow-list first so benign status lines are not caught by backend error tokens.
        if any(pattern.search(line) for pattern in LOG_ALLOW_PATTERNS):
            continue
        if any(pattern.search(line) for pattern in FAIL_PATTERNS):
            bad.append(f"{line_no}: {line}")
    return bad


def backend_smokes(v: Validator, build_dir: Path) -> None:
    for api in v.args.apis:
        smoke_backend(v, build_dir, api)


def player_smokes(v: Validator, build_dir: Path) -> None:
    if not v.args.player_smoke:
        v.add("player smoke", "SKIP", "player smoke disabled")
        return
    if not v.args.apis:
        v.add("player smoke", "SKIP", "No --apis selected")
        return
    for api in v.args.apis:
        smoke_player(v, build_dir, api)


def smoke_player(v: Validator, build_dir: Path, api: str) -> None:
    bin_dir = build_bin_dir(build_dir, v.args.config)
    exe = bin_dir / "PhasmaPlayer.exe"
    if not exe.exists():
        v.add(f"player {api} smoke", "FAIL", f"Missing executable: {exe}")
        raise RuntimeError(f"player {api} smoke")

    ensure_startup_files(bin_dir, v.args.scene)
    clear_engine_log(bin_dir)

    env = os.environ.copy()
    if v.args.validation:
        if api == "vulkan":
            env["PE_VULKAN_VALIDATION"] = "1"
        elif api == "dx12":
            env["PE_DX12_DEBUG"] = "1"
            env["PE_DX12_GBV"] = "1"
            env["PE_DX12_DRED"] = "1"

    start = time.monotonic()
    stdout_path = v.report_dir / f"player_{api}_smoke_stdout.log"
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout:
        proc = subprocess.Popen(
            [str(exe), "--api", api, "--display", str(v.args.display)],
            cwd=str(bin_dir),
            env=env,
            stdout=stdout,
            stderr=subprocess.STDOUT,
            text=True,
        )

    time.sleep(v.args.player_smoke_seconds)
    exited = proc.poll()
    if exited is not None:
        detail = f"Exited early with code 0x{exited & 0xFFFFFFFF:08X}"
        detail += "\n" + scan_engine_log(v, bin_dir, f"player_{api}")
        v.add(f"player {api} smoke", "FAIL", detail, time.monotonic() - start)
        raise RuntimeError(f"player {api} smoke")

    proc.kill()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass

    log_text = read_engine_log(bin_dir)
    log_detail = scan_engine_log(v, bin_dir, f"player_{api}")
    if not scene_load_marker_present(log_text, v.args.scene):
        v.add(
            f"player {api} smoke",
            "FAIL",
            f"Scene load marker missing for {v.args.scene}\n" + log_detail,
            time.monotonic() - start,
        )
        raise RuntimeError(f"player {api} smoke")

    bad_lines = find_bad_log_lines(log_text)
    if bad_lines:
        v.add(f"player {api} smoke", "FAIL", "Validation/error lines:\n" + "\n".join(bad_lines[:40]), time.monotonic() - start)
        raise RuntimeError(f"player {api} smoke")

    v.add(f"player {api} smoke", "PASS", tail(log_detail, 12), time.monotonic() - start)


def clear_engine_log(bin_dir: Path) -> None:
    log = bin_dir / "PhasmaEngine.log"
    try:
        log.unlink()
    except FileNotFoundError:
        pass
    except OSError:
        pass


def normalize_log_path(path: str) -> str:
    return path.strip().replace("\\", "/").lower()


def scene_load_marker_present(log_text: str, scene: str) -> bool:
    expected = normalize_log_path(scene)
    for line in log_text.splitlines():
        marker = "Scene loaded from:"
        if marker not in line:
            continue
        loaded = normalize_log_path(line.split(marker, 1)[1])
        if loaded == expected or loaded.endswith("/" + expected):
            return True
    return False


def display_marker_present(log_text: str, display: int) -> bool:
    return f"Creating window on display {display}/" in log_text


def copy_tool_path(path_text: str, destination_dir: Path) -> Path | None:
    if not path_text:
        return None
    path = local_path_from_tool(path_text)
    if not path.exists():
        return None
    destination_dir.mkdir(parents=True, exist_ok=True)
    dest = destination_dir / path.name
    shutil.copy2(path, dest)
    return dest


def local_path_from_tool(path_text: str) -> Path:
    path = Path(path_text)
    if path.exists():
        return path
    match = re.match(r"^([A-Za-z]):[\\/](.*)$", path_text)
    if match and os.name != "nt":
        drive = match.group(1).lower()
        rest = match.group(2).replace("\\", "/")
        return Path("/mnt") / drive / rest
    return path


def collect_screenshot(v: Validator) -> Path | None:
    if not v.mcp_client:
        v.add("screenshot capture", "SKIP", "MCP client unavailable")
        return None
    start = time.monotonic()
    try:
        result = call_tool_with_timeout(v, "take_screenshot", None, v.args.screenshot_timeout)
    except Exception as exc:
        v.add("screenshot capture", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("screenshot capture") from exc
    if result.get("isError"):
        v.add("screenshot capture", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
        raise RuntimeError("screenshot capture")
    data = result.get("structuredContent") or {}
    path = copy_tool_path(data.get("path", ""), v.report_dir / "screenshots")
    if not path:
        v.add("screenshot capture", "FAIL", f"Screenshot path missing or not found: {data}", time.monotonic() - start)
        raise RuntimeError("screenshot capture")
    (v.report_dir / "screenshot.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    v.add("screenshot capture", "PASS", rel(path), time.monotonic() - start)
    return path


def screenshot_sanity_step(v: Validator, path: Path | None) -> None:
    if not v.args.screenshot_sanity:
        v.add("screenshot sanity", "SKIP", "disabled")
        return
    if not path:
        v.add("screenshot sanity", "SKIP", "No screenshot captured")
        return
    start = time.monotonic()
    try:
        info = analyze_png(path)
    except Exception as exc:
        v.add("screenshot sanity", "FAIL", str(exc), time.monotonic() - start)
        raise RuntimeError("screenshot sanity") from exc

    failures = []
    if info["bytes"] < v.args.min_screenshot_bytes:
        failures.append(f"bytes {info['bytes']} < {v.args.min_screenshot_bytes}")
    if info["width"] < v.args.min_screenshot_width or info["height"] < v.args.min_screenshot_height:
        failures.append(
            f"dimensions {info['width']}x{info['height']} below "
            f"{v.args.min_screenshot_width}x{v.args.min_screenshot_height}"
        )
    if not info.get("nonflat", False):
        failures.append("image appears flat")
    if info.get("nonblack_ratio", 0.0) < v.args.min_screenshot_nonblack_ratio:
        failures.append(
            f"nonblack ratio {info.get('nonblack_ratio', 0.0):.4f} < {v.args.min_screenshot_nonblack_ratio:.4f}"
        )

    detail = (
        f"{info['width']}x{info['height']} bytes={info['bytes']} "
        f"unique_samples={info.get('unique_samples')} nonblack={info.get('nonblack_ratio', 0.0):.3f}"
    )
    if failures:
        v.add("screenshot sanity", "FAIL", detail + "\n" + "\n".join(failures), time.monotonic() - start)
        raise RuntimeError("screenshot sanity")
    v.add("screenshot sanity", "PASS", detail, time.monotonic() - start)


def analyze_png(path: Path) -> dict:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"Not a PNG: {path}")
    offset = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_data = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, _ = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break
    if width is None or height is None or bit_depth is None or color_type is None:
        raise ValueError("PNG missing IHDR")
    info = {"width": width, "height": height, "bytes": len(data), "color_type": color_type, "bit_depth": bit_depth}
    if bit_depth != 8 or color_type not in {0, 2, 6}:
        info.update({"nonflat": True, "unique_samples": "unsupported", "nonblack_ratio": 1.0})
        return info

    channels = {0: 1, 2: 3, 6: 4}[color_type]
    row_bytes = width * channels
    raw = zlib.decompress(bytes(idat))
    previous = bytearray(row_bytes)
    pixels = bytearray()
    pos = 0
    for _ in range(height):
        filter_type = raw[pos]
        pos += 1
        row = bytearray(raw[pos : pos + row_bytes])
        pos += row_bytes
        unfilter_png_row(row, previous, channels, filter_type)
        pixels.extend(row)
        previous = row

    unique_samples = set()
    nonblack = 0
    total = width * height
    sample_stride = max(channels, (total // 20000) * channels)
    for i in range(0, len(pixels), channels):
        rgb = pixels[i : i + min(3, channels)]
        if sum(rgb) > 10:
            nonblack += 1
        if i % sample_stride == 0:
            unique_samples.add(bytes(pixels[i : i + channels]))
            if len(unique_samples) > 32:
                break
    info["unique_samples"] = len(unique_samples)
    info["nonflat"] = len(unique_samples) > 1
    info["nonblack_ratio"] = nonblack / total if total else 0.0
    return info


def unfilter_png_row(row: bytearray, previous: bytearray, bpp: int, filter_type: int) -> None:
    for i in range(len(row)):
        left = row[i - bpp] if i >= bpp else 0
        up = previous[i]
        up_left = previous[i - bpp] if i >= bpp else 0
        if filter_type == 0:
            value = row[i]
        elif filter_type == 1:
            value = row[i] + left
        elif filter_type == 2:
            value = row[i] + up
        elif filter_type == 3:
            value = row[i] + ((left + up) // 2)
        elif filter_type == 4:
            value = row[i] + paeth_predictor(left, up, up_left)
        else:
            raise ValueError(f"Unsupported PNG filter {filter_type}")
        row[i] = value & 0xFF


def paeth_predictor(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    pa = abs(estimate - left)
    pb = abs(estimate - up)
    pc = abs(estimate - up_left)
    if pa <= pb and pa <= pc:
        return left
    if pb <= pc:
        return up
    return up_left


def compare_screenshot(v: Validator, current: Path | None) -> None:
    if not v.args.visual_baseline:
        v.add("visual parity", "SKIP", "No --visual-baseline supplied")
        return
    if not current:
        v.add("visual parity", "SKIP", "No current screenshot")
        return
    try:
        from PIL import Image, ImageChops, ImageStat
    except ModuleNotFoundError:
        v.add("visual parity", "SKIP", "Install pillow for image RMS comparison: pip install pillow")
        return

    baseline = latest_png(v.args.visual_baseline)
    if not baseline:
        v.add("visual parity", "FAIL", f"No baseline PNG found in {v.args.visual_baseline}")
        raise RuntimeError("visual parity")

    with Image.open(baseline).convert("RGB") as base, Image.open(current).convert("RGB") as cur:
        if base.size != cur.size:
            v.add("visual parity", "FAIL", f"Size mismatch: {base.size} vs {cur.size}")
            raise RuntimeError("visual parity")
        diff = ImageChops.difference(base, cur)
        stat = ImageStat.Stat(diff)
        rms = sum(value * value for value in stat.rms) ** 0.5

    detail = f"baseline={baseline}\ncurrent={current}\nrms={rms:.3f}\nthreshold={v.args.visual_rms_threshold:.3f}"
    status = "PASS" if rms <= v.args.visual_rms_threshold else "FAIL"
    v.add("visual parity", status, detail)
    if status == "FAIL":
        raise RuntimeError("visual parity")


def latest_png(path: Path) -> Path | None:
    if path.is_file():
        return path
    pngs = sorted(path.glob("*.png"), key=lambda p: p.stat().st_mtime, reverse=True)
    return pngs[0] if pngs else None


def collect_perf_snapshots(v: Validator) -> Path | None:
    if not v.args.perf_baseline:
        v.add("performance snapshots", "SKIP", "No --perf-baseline supplied")
        return None
    if not v.mcp_client:
        v.add("performance snapshots", "SKIP", "MCP client unavailable")
        return None

    current_dir = v.report_dir / "perf_current"
    current_dir.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    prepare_perf_scene(v)
    for idx in range(v.args.snapshot_count):
        result = v.mcp_client.call_tool("profiler_snapshot")
        if result.get("isError"):
            v.add("performance snapshots", "FAIL", json.dumps(result, indent=2), time.monotonic() - start)
            raise RuntimeError("performance snapshots")
        data = result.get("structuredContent") or {}
        copied = copy_tool_path(data.get("path", ""), current_dir)
        if not copied:
            v.add("performance snapshots", "FAIL", f"Snapshot path missing or not found: {data}", time.monotonic() - start)
            raise RuntimeError("performance snapshots")
        time.sleep(v.args.snapshot_interval)

    v.add("performance snapshots", "PASS", rel(current_dir), time.monotonic() - start)
    return current_dir


def prepare_perf_scene(v: Validator) -> None:
    assert v.mcp_client is not None
    lua_scene = scene_lua_path(v.args.scene)
    code = f"""
local requested_scene = {json.dumps(lua_scene)}
scene.load(requested_scene)
rhi.change_present_mode("immediate")
local m = engine.get_metrics()
local present = rhi.get_present_mode()
pe_log(string.format("[precommit:perf] requested scene=%s present=%s fps=%.2f", requested_scene, present, m.fps))
"""
    result = v.mcp_client.call_tool("execute_lua", {"code": code})
    if result.get("isError"):
        v.add("performance setup", "FAIL", json.dumps(result, indent=2))
        raise RuntimeError("performance setup")
    time.sleep(v.args.perf_warmup)

    verify = v.mcp_client.call_tool(
        "execute_lua",
        {
            "code": r"""
rhi.change_present_mode("immediate")
local m = engine.get_metrics()
local present = rhi.get_present_mode()
pe_log(string.format("[precommit:perf] verify present=%s fps=%.2f", present, m.fps))
""",
        },
    )
    text = json.dumps(verify)
    fps_match = re.search(r"fps=([0-9.]+)", text)
    detail = f"{lua_scene} requested; immediate present mode re-applied"
    if fps_match:
        fps = float(fps_match.group(1))
        detail += f"; fps={fps:.2f}"
        if fps <= 65.0:
            detail += " (possibly still vsync-limited)"
    v.add("performance setup", "PASS", detail)


def compare_perf(v: Validator, current_dir: Path | None) -> None:
    if not v.args.perf_baseline:
        return
    if not current_dir:
        v.add("performance compare", "SKIP", "No current snapshots")
        return
    comparer = TOOLS / "compare_snapshots.py"
    if not comparer.exists():
        v.add("performance compare", "FAIL", f"Missing required tool: {rel(comparer)}")
        raise RuntimeError("performance compare")
    baseline = v.args.perf_baseline
    v.run_cmd(
        "performance compare",
        [python_executable(), str(comparer), str(baseline), str(current_dir)],
        timeout=v.args.perf_compare_timeout,
        log_name="performance_compare",
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["quick", "commit", "full"], default="commit")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--targets", nargs="*", default=["PhasmaEditor", "PhasmaPlayer", "PhasmaLauncher"])
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-timeout", type=int, default=1800)
    launcher_group = parser.add_mutually_exclusive_group()
    launcher_group.add_argument("--launcher-smoke", dest="launcher_smoke", action="store_true", help="Launch PhasmaLauncher briefly")
    launcher_group.add_argument("--no-launcher-smoke", dest="launcher_smoke", action="store_false")
    parser.set_defaults(launcher_smoke=None)
    parser.add_argument("--launcher-smoke-seconds", type=int, default=5)
    parser.add_argument("--mcp-url", default="http://127.0.0.1:8765/mcp")
    parser.add_argument("--mcp-timeout", type=float, default=5.0)
    parser.add_argument("--mcp-tool-timeout", type=float, default=30.0)
    parser.add_argument("--require-mcp", action="store_true", default=True)
    parser.add_argument("--display", type=int, default=1, help="Display index for launched Editor/Player windows")
    parser.add_argument("--editor-api", choices=["vulkan", "dx12"], default="vulkan", help="API used for the MCP editor")
    parser.add_argument("--editor-start-timeout", type=int, default=60)
    launch_group = parser.add_mutually_exclusive_group()
    launch_group.add_argument("--launch-editor", dest="launch_editor", action="store_true", default=True)
    launch_group.add_argument("--no-launch-editor", dest="launch_editor", action="store_false")
    adk_group = parser.add_mutually_exclusive_group()
    adk_group.add_argument("--adk-smoke", dest="adk_smoke", action="store_true", help="Run one ADK CLI smoke prompt")
    adk_group.add_argument("--no-adk-smoke", dest="adk_smoke", action="store_false")
    parser.set_defaults(adk_smoke=None)
    parser.add_argument("--require-adk", action="store_true")
    parser.add_argument("--adk-timeout", type=int, default=180)
    mcp_internal_group = parser.add_mutually_exclusive_group()
    mcp_internal_group.add_argument("--mcp-internal-smoke", dest="mcp_internal_smoke", action="store_true")
    mcp_internal_group.add_argument("--no-mcp-internal-smoke", dest="mcp_internal_smoke", action="store_false")
    parser.set_defaults(mcp_internal_smoke=None)
    console_group = parser.add_mutually_exclusive_group()
    console_group.add_argument("--console-scan", dest="console_scan", action="store_true")
    console_group.add_argument("--no-console-scan", dest="console_scan", action="store_false")
    parser.set_defaults(console_scan=None)
    parser.add_argument("--console-scan-count", type=int, default=400)
    play_group = parser.add_mutually_exclusive_group()
    play_group.add_argument("--play-lifecycle", dest="play_lifecycle", action="store_true")
    play_group.add_argument("--no-play-lifecycle", dest="play_lifecycle", action="store_false")
    parser.set_defaults(play_lifecycle=None)
    parser.add_argument("--play-lifecycle-pause", type=float, default=0.5)
    parser.add_argument("--play-lifecycle-timeout", type=float, default=90.0)
    hot_reload_group = parser.add_mutually_exclusive_group()
    hot_reload_group.add_argument("--hot-reload-smoke", dest="hot_reload_smoke", action="store_true")
    hot_reload_group.add_argument("--no-hot-reload-smoke", dest="hot_reload_smoke", action="store_false")
    parser.set_defaults(hot_reload_smoke=None)
    parser.add_argument("--hot-reload-timeout", type=int, default=30)
    script_tests_group = parser.add_mutually_exclusive_group()
    script_tests_group.add_argument("--script-tests", dest="script_tests", action="store_true")
    script_tests_group.add_argument("--no-script-tests", dest="script_tests", action="store_false")
    parser.set_defaults(script_tests=None)
    parser.add_argument("--script-tests-wait", type=float, default=3.0)
    parser.add_argument("--apis", nargs="*", default=["vulkan", "dx12"])
    parser.add_argument("--scene", default=DEFAULT_SCENE)
    parser.add_argument("--smoke-seconds", type=int, default=18)
    player_group = parser.add_mutually_exclusive_group()
    player_group.add_argument("--player-smoke", dest="player_smoke", action="store_true", help="Launch PhasmaPlayer smokes")
    player_group.add_argument("--no-player-smoke", dest="player_smoke", action="store_false")
    parser.set_defaults(player_smoke=None)
    parser.add_argument("--player-smoke-seconds", type=int, default=18)
    parser.add_argument("--validation", action="store_true", help="Enable Vulkan/DX12 validation env vars for backend smokes")
    parser.add_argument("--log-lines", type=int, default=400)
    parser.add_argument("--screenshot", action="store_true", help="Capture a current screenshot through MCP")
    parser.add_argument("--screenshot-timeout", type=float, default=30.0)
    screenshot_sanity_group = parser.add_mutually_exclusive_group()
    screenshot_sanity_group.add_argument("--screenshot-sanity", dest="screenshot_sanity", action="store_true")
    screenshot_sanity_group.add_argument("--no-screenshot-sanity", dest="screenshot_sanity", action="store_false")
    parser.set_defaults(screenshot_sanity=None)
    parser.add_argument("--min-screenshot-bytes", type=int, default=4096)
    parser.add_argument("--min-screenshot-width", type=int, default=64)
    parser.add_argument("--min-screenshot-height", type=int, default=64)
    parser.add_argument("--min-screenshot-nonblack-ratio", type=float, default=0.01)
    parser.add_argument("--visual-baseline", type=Path, help="PNG file or directory for optional visual parity check")
    parser.add_argument("--visual-rms-threshold", type=float, default=4.0)
    parser.add_argument("--perf-baseline", type=Path, help="Snapshot JSON file or directory for performance comparison")
    parser.add_argument("--snapshot-count", type=int, default=10)
    parser.add_argument("--snapshot-interval", type=float, default=1.0)
    parser.add_argument("--perf-warmup", type=float, default=5.0)
    parser.add_argument("--perf-compare-timeout", type=int, default=120)
    parser.add_argument("--report-dir", type=Path)
    return parser.parse_args(argv)


def apply_profile_defaults(args: argparse.Namespace) -> None:
    if args.profile == "quick":
        args.skip_build = True
        args.apis = []
        args.validation = False
        if args.launcher_smoke is None:
            args.launcher_smoke = False
        if args.player_smoke is None:
            args.player_smoke = False
        if args.adk_smoke is None:
            args.adk_smoke = True
        if args.mcp_internal_smoke is None:
            args.mcp_internal_smoke = False
        if args.console_scan is None:
            args.console_scan = False
        if args.play_lifecycle is None:
            args.play_lifecycle = False
        if args.hot_reload_smoke is None:
            args.hot_reload_smoke = False
        if args.script_tests is None:
            args.script_tests = False
        if args.screenshot_sanity is None:
            args.screenshot_sanity = False
    elif args.profile == "full":
        args.validation = True
        args.screenshot = True
        if args.adk_smoke is None:
            args.adk_smoke = True
        args.require_adk = True
        if args.launcher_smoke is None:
            args.launcher_smoke = True
        if args.player_smoke is None:
            args.player_smoke = True
        if args.mcp_internal_smoke is None:
            args.mcp_internal_smoke = True
        if args.console_scan is None:
            args.console_scan = True
        if args.play_lifecycle is None:
            args.play_lifecycle = False
        if args.hot_reload_smoke is None:
            args.hot_reload_smoke = False
        if args.script_tests is None:
            args.script_tests = False
        if args.screenshot_sanity is None:
            args.screenshot_sanity = True
    elif args.profile == "commit":
        args.screenshot = True
        if args.launcher_smoke is None:
            args.launcher_smoke = True
        if args.player_smoke is None:
            args.player_smoke = True
        if args.adk_smoke is None:
            args.adk_smoke = True
        if args.mcp_internal_smoke is None:
            args.mcp_internal_smoke = True
        if args.console_scan is None:
            args.console_scan = True
        if args.play_lifecycle is None:
            args.play_lifecycle = False
        if args.hot_reload_smoke is None:
            args.hot_reload_smoke = False
        if args.script_tests is None:
            args.script_tests = False
        if args.screenshot_sanity is None:
            args.screenshot_sanity = True
    if args.launcher_smoke is None:
        args.launcher_smoke = False
    if args.player_smoke is None:
        args.player_smoke = False
    if args.adk_smoke is None:
        args.adk_smoke = False
    if args.mcp_internal_smoke is None:
        args.mcp_internal_smoke = False
    if args.console_scan is None:
        args.console_scan = False
    if args.play_lifecycle is None:
        args.play_lifecycle = False
    if args.hot_reload_smoke is None:
        args.hot_reload_smoke = False
    if args.script_tests is None:
        args.script_tests = False
    if args.screenshot_sanity is None:
        args.screenshot_sanity = False


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    apply_profile_defaults(args)
    build_dir = choose_build_dir(args.build_dir)
    validator = Validator(args)

    try:
        git_status_step(validator)
        hygiene_steps(validator)
        if args.skip_build:
            validator.add("build", "SKIP", "--skip-build active")
        else:
            build_step(validator, build_dir)
            launcher_smoke_step(validator, build_dir)
        launch_editor_for_mcp(validator, build_dir)
        editor_display_assertion_step(validator)
        mcp_probe_step(validator)
        mcp_internal_smoke_step(validator)
        console_log_scan_step(validator, "startup")
        if args.adk_smoke:
            adk_smoke_step(validator)
            console_log_scan_step(validator, "adk")
        current_screenshot = collect_screenshot(validator) if args.screenshot or args.visual_baseline else None
        screenshot_sanity_step(validator, current_screenshot)
        console_log_scan_step(validator, "screenshot")
        compare_screenshot(validator, current_screenshot)
        editor_play_lifecycle_step(validator)
        console_log_scan_step(validator, "play")
        script_tests_step(validator)
        hot_reload_smoke_step(validator)
        console_log_scan_step(validator, "hot_reload")
        current_perf = collect_perf_snapshots(validator)
        compare_perf(validator, current_perf)
        if args.apis:
            validator.cleanup_processes("editor-mcp")
            backend_smokes(validator, build_dir)
        player_smokes(validator, build_dir)
    except RuntimeError:
        pass
    except Exception as exc:
        validator.add("validator internal error", "FAIL", repr(exc))

    return validator.finish()


if __name__ == "__main__":
    raise SystemExit(main())
