"""Writes the WebGPU limits of Chrome's adapter on this machine: {"limits": {...}, "features": [...], "adapter": {...}}.

The browser backend requests the adapter's maximum limits, so these are the limits the web build runs with. Desktop
Vulkan under PHASMA_CAPS_PROFILE=webgpu clamps its own limits to them (RHI.cpp ReadWebGpuLimits reads the file named by
PHASMA_WEBGPU_LIMITS, else webgpu_limits.json next to the executable; without either it uses WebGPU's defaults).

Usage: query_webgpu_limits.py OUT.json [--chrome PATH] [--seconds N]
Exit 0 written, 1 Chrome reported no adapter or did not answer, 77 Chrome not found.
"""
import argparse
import http.server
import json
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

CHROME = Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe")

# GPUSupportedLimits exposes its limits as enumerable prototype attributes, so for-in lists every one Chrome knows.
PAGE = b"""<!doctype html><script>
(async () => {
  let body;
  try {
    const adapter = await navigator.gpu.requestAdapter({powerPreference: "high-performance"});
    const limits = {};
    for (const name in adapter.limits)
      limits[name] = adapter.limits[name];
    const info = adapter.info || {};
    body = {
      limits,
      features: [...adapter.features].sort(),
      adapter: {vendor: info.vendor, architecture: info.architecture, device: info.device,
                description: info.description},
    };
  } catch (e) {
    body = {error: String(e)};
  }
  await fetch("/result", {method: "POST", body: JSON.stringify(body)});
})();
</script>"""


def serve(result):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(PAGE)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            result.append(json.loads(self.rfile.read(length)))
            self.send_response(204)
            self.end_headers()

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out", type=Path)
    parser.add_argument("--chrome", type=Path, default=CHROME)
    parser.add_argument("--seconds", type=float, default=60)
    args = parser.parse_args()

    if not args.chrome.exists():
        print(f"SKIP: no Chrome at {args.chrome}")
        return 77

    result = []
    server = serve(result)
    profile = tempfile.mkdtemp(prefix="pe-webgpu-limits-")
    chrome = subprocess.Popen([str(args.chrome), f"--user-data-dir={profile}", "--headless=new", "--no-first-run",
                               "--enable-unsafe-webgpu", f"http://127.0.0.1:{server.server_address[1]}/"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + args.seconds
        while not result and time.monotonic() < deadline:
            time.sleep(0.5)
    finally:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(chrome.pid)], capture_output=True)
        server.shutdown()
        time.sleep(1)
        shutil.rmtree(profile, ignore_errors=True)

    if not result:
        print(f"FAIL: Chrome did not report within {args.seconds:.0f} s")
        return 1

    if "error" in result[0]:
        print(f"FAIL: {result[0]['error']}")
        return 1

    args.out.write_text(json.dumps(result[0], indent=2) + "\n", encoding="utf-8")
    limits = result[0]["limits"]
    adapter = result[0]["adapter"]
    print(f"wrote {args.out}: {len(limits)} limits, {len(result[0]['features'])} features "
          f"({adapter.get('vendor')} {adapter.get('architecture')}); "
          f"{limits.get('maxSampledTexturesPerShaderStage')} sampled textures and "
          f"{limits.get('maxStorageBuffersPerShaderStage')} storage buffers per stage")
    return 0


if __name__ == "__main__":
    sys.exit(main())
