"""Convert a desktop Vulkan shader cache to the browser's (WebGPU RHI spec decision 7).

For each ShaderCache/_spv/<key> (or only the given keys) writes ShaderCache/_wgsl/<key> (WGSL) and
ShaderCache/_wgsl/<key>.bindings (the binding sidecar). The cache must come from a desktop Vulkan run with
PHASMA_SPIRV_TARGET=1.2, so `discard` is OpKill and the keys are the ones the browser computes.

Pre-steps naga needs on the engine's SPIR-V (each verified in the P0 census, docs/wiki/architecture/rendering.md):
strip debug-line instructions, lower the flat-struct OpCopyLogical copies, then
`spirv-opt --legalize-hlsl --split-combined-image-sampler`. Phasma/WebGPU/SpvToWgsl does the rest.

Usage: python tools/convert_web_shaders.py <ShaderCache dir> [key ...]
Exit 0 when every module converts, 1 otherwise (each failure is listed).
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CRATE = ROOT / "Phasma" / "WebGPU" / "SpvToWgsl"
CONVERTER = CRATE / "target" / "release" / ("spv-to-wgsl.exe" if os.name == "nt" else "spv-to-wgsl")

# OpSourceContinued 2, OpSource 3, OpLine 8, OpNoLine 317, OpModuleProcessed 330: debug only, naga 29 rejects them.
DEBUG_OPS = {2, 3, 8, 317, 330}
OP_TYPE_STRUCT = 30
OP_COMPOSITE_CONSTRUCT = 80
OP_COMPOSITE_EXTRACT = 81
OP_COPY_LOGICAL = 400
NON_AGGREGATE_TYPES = {20, 21, 22, 23}  # OpTypeBool, OpTypeInt, OpTypeFloat, OpTypeVector


def read_instructions(data):
    words = list(memoryview(data).cast("I"))
    header = words[:5]
    instructions = []
    i = 5
    while i < len(words):
        count = words[i] >> 16
        if count == 0:
            raise ValueError(f"zero-length instruction at word {i}")

        instructions.append(words[i:i + count])
        i += count

    return header, instructions


def write_instructions(header, instructions):
    flat = header + [w for inst in instructions for w in inst]
    return b"".join(w.to_bytes(4, "little") for w in flat)


def lower_copy_logical(header, instructions):
    """naga has no OpCopyLogical. Rewrite copies of flat structs (scalar or vector members) as one OpCompositeExtract
    per member plus an OpCompositeConstruct. Returns how many copies were left (they fail the conversion)."""
    type_op = {inst[1]: inst[0] & 0xFFFF for inst in instructions if 19 <= (inst[0] & 0xFFFF) <= 39}
    structs = {inst[1]: inst[2:] for inst in instructions if inst[0] & 0xFFFF == OP_TYPE_STRUCT}
    bound = header[3]
    out = []
    left = 0
    for inst in instructions:
        if inst[0] & 0xFFFF != OP_COPY_LOGICAL:
            out.append(inst)
            continue

        result_type, result, source = inst[1], inst[2], inst[3]
        members = structs.get(result_type)
        flat = members and all(type_op.get(m) in NON_AGGREGATE_TYPES for m in members)
        if not flat:
            out.append(inst)
            left += 1
            continue

        parts = []
        for index, member_type in enumerate(members):
            out.append([(5 << 16) | OP_COMPOSITE_EXTRACT, member_type, bound, source, index])
            parts.append(bound)
            bound += 1

        out.append([((3 + len(parts)) << 16) | OP_COMPOSITE_CONSTRUCT, result_type, result, *parts])

    header = header[:3] + [bound] + header[4:]
    return header, out, left


def find_spirv_opt():
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidate = Path(sdk) / "Bin" / ("spirv-opt.exe" if os.name == "nt" else "spirv-opt")
        if candidate.exists():
            return str(candidate)

    return shutil.which("spirv-opt")


def ensure_converter():
    if CONVERTER.exists():
        return

    subprocess.run(["cargo", "build", "--release"], cwd=CRATE, check=True)


def run(args):
    p = subprocess.run(args, capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr).strip()


def convert(spv, out_dir, spirv_opt, scratch):
    """Returns None on success, else the reason."""
    key = spv.name
    header, instructions = read_instructions(spv.read_bytes())
    instructions = [inst for inst in instructions if (inst[0] & 0xFFFF) not in DEBUG_OPS]
    header, instructions, left = lower_copy_logical(header, instructions)
    if left:
        return f"{left} OpCopyLogical of a nested struct left (naga cannot read them)"

    lowered = scratch / f"{key}.lowered.spv"
    legal = scratch / f"{key}.legal.spv"
    lowered.write_bytes(write_instructions(header, instructions))
    code, message = run([spirv_opt, "--legalize-hlsl", "--split-combined-image-sampler", str(lowered), "-o", str(legal)])
    if code != 0:
        return f"spirv-opt: {message}"

    code, message = run([str(CONVERTER), str(legal), str(out_dir / key), str(out_dir / f"{key}.bindings")])
    if code != 0:
        return f"spv-to-wgsl exit {code}: {message}"

    if message:
        print(f"  {key}: {message}")

    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    cache = Path(sys.argv[1])
    spv_dir = cache / "_spv"
    out_dir = cache / "_wgsl"
    keys = sys.argv[2:]
    modules = [spv_dir / k for k in keys] if keys else sorted(p for p in spv_dir.iterdir() if p.is_file())
    missing = [m.name for m in modules if not m.exists()]
    if missing:
        print(f"[convert] ERROR: no SPIR-V for {', '.join(missing)} in {spv_dir}")
        return 1

    spirv_opt = find_spirv_opt()
    if not spirv_opt:
        print("[convert] ERROR: spirv-opt not found (set VULKAN_SDK or put it on PATH)")
        return 1

    ensure_converter()
    out_dir.mkdir(exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix="pe-wgsl-") as scratch:
        for spv in modules:
            reason = convert(spv, out_dir, spirv_opt, Path(scratch))
            if reason:
                failures.append((spv.name, reason))

    for key, reason in failures:
        print(f"[convert] FAIL {key}: {reason[:600]}")

    print(f"[convert] {len(modules) - len(failures)} of {len(modules)} modules converted into {out_dir}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
