#!/usr/bin/env python3
"""Headless PhasmaProfiler stream capture.

Connects to a ProfilerStreamServer (default 127.0.0.1:9876 — for Android run
`adb forward tcp:9876 tcp:9876` first), records length-prefixed JSON snapshots
for N seconds, writes them to a .jsonl file, and prints a summary (FPS median /
1% low, frame-time tails, and top CPU/GPU scope medians and maxima).

Usage: python tools/profiler_capture.py --seconds 60 --out baseline.jsonl
"""
import argparse
import json
import socket
import statistics
import struct
import sys
import time

REFRESH_RATES = {"4": 0, "10": 1, "30": 2, "60": 3, "per-frame": 4}


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("stream closed")
        buf.extend(chunk)
    return bytes(buf)


def compact_snapshot(frame, scope_threshold_ms):
    cpu = frame.get("cpu", {})
    gpu = frame.get("gpu", {})
    return {
        "overview": frame.get("overview", {}),
        "frame_history": frame.get("frame_history", []),
        "cpu": {
            "total_ms": cpu.get("total_ms", 0.0),
            "scopes": [
                scope
                for scope in cpu.get("scopes", [])
                if float(scope.get("cur_ms", 0.0)) >= scope_threshold_ms
            ],
        },
        "gpu": gpu,
        "counters": frame.get("counters", []),
    }


def capture(host, port, seconds, out_path, refresh_rate, scope_threshold_ms):
    sock = socket.create_connection((host, port), timeout=10)
    sock.settimeout(10)
    sock.sendall(bytes([REFRESH_RATES[refresh_rate]]))
    frames = []
    t0 = time.time()
    next_report = 5.0
    with open(out_path, "w", encoding="utf-8") as f:
        while (elapsed := time.time() - t0) < seconds:
            try:
                (length,) = struct.unpack("<I", recv_exact(sock, 4))
                payload = recv_exact(sock, length).decode("utf-8", "replace")
            except (ConnectionError, OSError) as e:
                print(f"  stream ended early at {elapsed:.1f}s: {e}", file=sys.stderr)
                break
            try:
                frame = json.loads(payload)
                if scope_threshold_ms is not None:
                    frame = compact_snapshot(frame, scope_threshold_ms)
                frames.append(frame)
                f.write(json.dumps(frame, separators=(",", ":")) + "\n")
            except json.JSONDecodeError as e:
                print(f"  [warn] bad JSON frame at {elapsed:.1f}s: {e}", file=sys.stderr)
            if elapsed >= next_report:
                fps = frames[-1].get("overview", {}).get("fps") if frames else None
                print(f"  {elapsed:5.1f}s  {len(frames)} snapshots  fps={fps}")
                next_report += 5.0
    sock.close()
    return frames


def med_by_name(items_per_frame, name_key="name"):
    by_name = {}
    for items in items_per_frame:
        for it in items:
            ms = it.get("cur_ms", it.get("ms")) if isinstance(it, dict) else None
            if ms is not None and name_key in it:
                by_name.setdefault(it[name_key], []).append(float(ms))
    return sorted(
        ((n, statistics.median(v), len(v)) for n, v in by_name.items()),
        key=lambda x: -x[1],
    )


def max_by_name(items_per_frame, name_key="name"):
    maxima = {}
    for items in items_per_frame:
        for it in items:
            ms = it.get("cur_ms", it.get("ms")) if isinstance(it, dict) else None
            if ms is not None and name_key in it:
                maxima[it[name_key]] = max(maxima.get(it[name_key], 0.0), float(ms))
    return sorted(maxima.items(), key=lambda row: -row[1])


def percentile(values, q):
    ordered = sorted(values)
    position = (len(ordered) - 1) * q
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(frames, budget_ms):
    fps = [f["overview"]["fps"] for f in frames if f.get("overview", {}).get("fps")]
    if fps:
        lows = sorted(fps)[: max(1, len(fps) // 100)]
        print(f"\nFPS: median {statistics.median(fps):.1f}  min {min(fps):.1f}  "
              f"1%low {statistics.median(lows):.1f}  ({len(fps)} samples)")
    frame_times = [
        float(history["frame_ms"])
        for frame in frames
        for history in frame.get("frame_history", [])
        if history.get("frame_ms") is not None
    ]
    if frame_times:
        print(
            f"Frame time: median {statistics.median(frame_times):.3f} ms  "
            f"p95 {percentile(frame_times, 0.95):.3f}  "
            f"p99 {percentile(frame_times, 0.99):.3f}  max {max(frame_times):.3f}  "
            f">={budget_ms:g} ms {sum(ms >= budget_ms for ms in frame_times)}/{len(frame_times)}"
        )
    for label, key, sub in (("CPU scopes", "cpu", "scopes"), ("GPU passes", "gpu", "passes")):
        items_per_frame = [f.get(key, {}).get(sub, []) for f in frames]
        rows = med_by_name(items_per_frame)
        if rows:
            print(f"\nTop {label} (median ms):")
            for name, ms, n in rows[:15]:
                print(f"  {ms:8.3f}  {name}  [{n}]")
        maxima = max_by_name(items_per_frame)
        if maxima:
            print(f"\nTop {label} (max ms):")
            for name, ms in maxima[:15]:
                print(f"  {ms:8.3f}  {name}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9876)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out", default="profiler_capture.jsonl")
    ap.add_argument("--refresh-rate", choices=REFRESH_RATES, default="4")
    ap.add_argument(
        "--compact",
        action="store_true",
        help="keep frame data, counters, GPU passes, and CPU scopes at or above the threshold",
    )
    ap.add_argument(
        "--scope-threshold-ms",
        type=float,
        default=0.05,
        help="CPU scope threshold used with --compact (default: 0.05 ms)",
    )
    ap.add_argument(
        "--budget-ms",
        type=float,
        default=20.0,
        help="frame-time threshold reported by the summary (default: 20 ms)",
    )
    ap.add_argument("--summarize-only", metavar="JSONL", help="skip capture, summarize an existing file")
    args = ap.parse_args()

    if args.summarize_only:
        with open(args.summarize_only, encoding="utf-8") as f:
            frames = [json.loads(line) for line in f if line.strip()]
    else:
        print(f"Capturing {args.seconds:.0f}s from {args.host}:{args.port} -> {args.out}")
        threshold = max(0.0, args.scope_threshold_ms) if args.compact else None
        frames = capture(args.host, args.port, args.seconds, args.out, args.refresh_rate, threshold)
        print(f"Captured {len(frames)} snapshots -> {args.out}")
    if frames:
        summarize(frames, max(0.0, args.budget_ms))
    else:
        print("No snapshots captured.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
