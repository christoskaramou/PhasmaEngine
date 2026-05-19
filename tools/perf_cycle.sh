#!/usr/bin/env bash
# Usage: ./tools/perf_cycle.sh [Debug|Release] [build-dir]
# Exit: 0=ready, 1=build failed, 2=editor timed out

set -euo pipefail

CONFIG="${1:-Debug}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${2:-${PHASMA_BUILD_DIR:-$ROOT_DIR/build}}"
BIN_DIR="$BUILD_DIR/$CONFIG"

to_windows_path() {
    if command -v wslpath >/dev/null 2>&1; then
        wslpath -w "$1"
    elif command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$1"
    else
        printf "%s" "$1"
    fi
}

# 1. Stop editor (allowed to fail — editor may not be running)
if command -v taskkill.exe >/dev/null 2>&1; then
    taskkill.exe //F //IM PhasmaEditor.exe 2>/dev/null && echo "  [perf_cycle] editor stopped" || echo "  [perf_cycle] editor not running"
elif command -v taskkill >/dev/null 2>&1; then
    taskkill //F //IM PhasmaEditor.exe 2>/dev/null && echo "  [perf_cycle] editor stopped" || echo "  [perf_cycle] editor not running"
else
    echo "  [perf_cycle] taskkill not found; assuming editor is not running"
fi
sleep 1

# 2. Build
cmake --build "$BUILD_DIR" --config "$CONFIG" --target PhasmaEditor || {
    echo "[perf_cycle] BUILD FAILED"
    exit 1
}

# 3. Launch detached
cd "$BIN_DIR"
if command -v powershell.exe >/dev/null 2>&1; then
    WIN_BIN_DIR="$(to_windows_path "$BIN_DIR")"
    powershell.exe -NoProfile -Command "Start-Process -FilePath '$WIN_BIN_DIR\\PhasmaEditor.exe' -WorkingDirectory '$WIN_BIN_DIR'" >/dev/null
elif command -v cmd.exe >/dev/null 2>&1; then
    cmd.exe //C start "" PhasmaEditor.exe
else
    ./PhasmaEditor.exe >/dev/null 2>&1 &
fi
cd - >/dev/null

# 4. Wait for MCP endpoint (30s timeout)
TIMEOUT=30
ELAPSED=0
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 1 http://127.0.0.1:8765/mcp || true)
    if [ "$HTTP_CODE" != "000" ]; then
        echo "[perf_cycle] READY after ${ELAPSED}s"
        exit 0
    fi
    sleep 1
    ELAPSED=$(( ELAPSED + 1 ))
done

echo "[perf_cycle] ERROR: timed out after 30s"
exit 2
