#!/usr/bin/env python3
"""Ask Jev (TypeSafe System One) which change to test first, from a Profiler advice report.

python tools/jev_advise.py ProfilerCaptures/advice_<time>.json [--dry]

Input is the report the AI Advisor tab saves (or `PhasmaProfiler --advise capture.jsonl` prints).
Jev receives the measured evidence, hardware/settings context and the report's options; never the
scene path. --dry prints the request without a key or network call. Always prints one JSON object.
Key: JEV_API_KEY / TYPESAFE_API_KEY, Windows Credential Manager jev/api-key, or typesafeApiKey in
%APPDATA%/jev-model-router/config.json.
"""
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ENDPOINT = "https://api.typesafe.ai/v1/systemone"
CONTEXT_KEYS = ("graphics_api", "gpu_name", "platform", "cpu_logical_cores", "build_configuration",
                "present_mode", "display_width", "display_height", "render_width", "render_height", "settings")
INSTRUCTIONS = (
    "Pick the single change to test first for smooth frames at the target. evidence.spikes.verdict is measured, not a "
    "guess: recurring means the frames stutter and need a fix. session.summary is the whole session in time order "
    "(rows of row_s seconds) and session.trend says whether spikes rose over it. evidence.costs covers every frame: "
    "intermittent work runs in few frames yet adds up, and wait scopes are the CPU blocked on the GPU or driver, a "
    "symptom rather than a cause. Spike counts are hitches: consecutive slow frames count once. evidence.slowdowns "
    "counts frames 1.25-2x slower than usual; compare its extra_ms_per_minute with the spikes': many small "
    "slowdowns can lose more time, and costs.cpu_variation names the work that varies most. Weigh the measured cost against "
    "the visible quality loss: prefer the change that removes the time behind missed frames while costing the "
    "least visible quality. A change whose affected passes are cheap cannot fix a large overrun. Spikes are "
    "frames at least twice the median: visible stutter even when every frame fits the budget. When recurring "
    "spikes come from one scope or pass, prefer fixing that source over lowering quality. Choose no_change only "
    "when frames meet the budget without recurring spikes, or the evidence is insufficient."
)
VAULT_READ = (
    "$ErrorActionPreference='Stop';"
    "[void][Windows.Security.Credentials.PasswordVault,Windows.Security.Credentials,ContentType=WindowsRuntime];"
    "try { $c=(New-Object Windows.Security.Credentials.PasswordVault).Retrieve('jev','api-key');"
    "$c.RetrievePassword(); [Console]::Out.Write($c.Password) } catch { exit 3 }"
)


def api_key():
    for name in ("JEV_API_KEY", "TYPESAFE_API_KEY"):
        if os.environ.get(name, "").strip():
            return os.environ[name].strip()
    if os.name == "nt":
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", VAULT_READ],
                                capture_output=True, text=True, timeout=15)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    config = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")) / "jev-model-router" / "config.json"
    try:
        return json.loads(config.read_text(encoding="utf-8"))["typesafeApiKey"].strip() or None
    except (OSError, ValueError, KeyError, AttributeError):
        return None


def build_request(report):
    options = report.get("options") or []
    if len(options) < 2 or report.get("jev_ready") is False:
        raise ValueError("not enough evidence yet: collect at least 30 s of frames with unchanged settings")
    context = report.get("context") or {}
    evidence = report.get("evidence") or {}
    state = {
        "goal": f"Reach a steady {report.get('target_fps')} FPS ({evidence.get('budget_ms')} ms per frame) "
                "with the best image quality the budget allows.",
        "hardware_and_settings": {key: context[key] for key in CONTEXT_KEYS if key in context},
        "evidence": evidence,
    }
    # The whole session as at most 60 merged rows; the saved file's 10 s rows and spike events stay local.
    session = report.get("session")
    if session:
        state["session"] = {key: session[key] for key in ("duration_s", "trend", "first_half_spikes_per_minute",
                                                          "second_half_spikes_per_minute", "row_s", "summary") if key in session}
    criteria = {}
    for option in options:
        cost = option.get("affected_gpu_ms")
        measured = "not measured" if cost is None else f"{cost} ms"
        criteria[option["id"]] = f"{option['description']} Measured GPU time of the affected passes: {measured}."
    return {
        "state": state,
        "model": os.environ.get("JEV_MODEL", "jev-latest"),
        "questions": {"next_change": {"type": "choice", "instructions": INSTRUCTIONS, "criteria": criteria}},
    }


def ask(report, request):
    key = api_key()
    if not key:
        return {"error": "No Jev key: set JEV_API_KEY or save Credential Manager entry jev/api-key."}
    http = urllib.request.Request(ENDPOINT, data=json.dumps(request).encode("utf-8"), method="POST",
                                  headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(http, timeout=60) as response:
            body = json.load(response)
    except urllib.error.HTTPError as error:
        return {"error": f"Jev HTTP {error.code}: {error.read()[:300].decode('utf-8', 'replace')}"}
    answer = (body.get("answers") or {}).get("next_change") or {}
    options = {option["id"]: option for option in report["options"]}
    ranking = sorted(({"id": option_id, "probability": round(float(probability), 3),
                       "description": options[option_id]["description"],
                       "affected_gpu_ms": options[option_id].get("affected_gpu_ms")}
                      for option_id, probability in (answer.get("probabilities") or {}).items() if option_id in options),
                     key=lambda row: -row["probability"])
    return {"model": body.get("model"), "choice": answer.get("choice") if answer.get("choice") in options else None,
            "confidence": answer.get("confidence"), "ranking": ranking, "usage": body.get("usage")}


def main():
    args = sys.argv[1:]
    dry = "--dry" in args
    paths = [arg for arg in args if arg != "--dry"]
    try:
        if len(paths) != 1:
            raise ValueError("usage: jev_advise.py advice_report.json [--dry]")
        report = json.loads(Path(paths[0]).read_text(encoding="utf-8"))
        request = build_request(report)
        result = {"dry_run": True, "request": request} if dry else ask(report, request)
    except Exception as error:  # one JSON line for the Profiler, whatever failed
        result = {"error": f"{type(error).__name__}: {error}"}
    print(json.dumps(result))
    return 1 if "error" in result else 0


if __name__ == "__main__":
    sys.exit(main())
