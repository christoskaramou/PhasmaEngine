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
OP_SAMPLED_IMAGE = 86
OP_IMAGE = 100
# Image operations whose words are [opcode, result type, result id, image, ...]: fetch, read and the size queries.
IMAGE_OPERAND_OPS = {95, 98, 103, 104, 106, 107}
OP_TYPE_ARRAY = 28
OP_CONSTANT = 43
# Instructions whose words are [opcode, result type, result id, ...]: what an OpCopyLogical source can come from.
VALUE_OPS = {55, 57, 61, 80, 81, 83, 169, 245, 400}


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
    """naga has no OpCopyLogical. It copies between two struct (or array) types that are laid out alike but declared
    apart, so rewrite each copy as OpCompositeExtract per member plus OpCompositeConstruct of the result type, one level
    at a time: a member whose types match is extracted as is, a nested struct or array is rebuilt the same way.
    Returns how many copies were left (they fail the conversion)."""
    structs = {inst[1]: inst[2:] for inst in instructions if inst[0] & 0xFFFF == OP_TYPE_STRUCT}
    arrays = {inst[1]: (inst[2], inst[3]) for inst in instructions if inst[0] & 0xFFFF == OP_TYPE_ARRAY}
    constants = {inst[2]: inst[3] for inst in instructions if inst[0] & 0xFFFF == OP_CONSTANT and len(inst) == 4}
    value_types = {inst[2]: inst[1] for inst in instructions if inst[0] & 0xFFFF in VALUE_OPS}
    bound = header[3]

    def members(type_id):
        if type_id in structs:
            return list(structs[type_id])

        if type_id in arrays and arrays[type_id][1] in constants:
            element, length = arrays[type_id]
            return [element] * constants[length]

        return None

    def rebuild(out, value, from_type, to_type):
        """Emits a value of to_type from one of the alike from_type; None when they are not alike aggregates."""
        nonlocal bound
        if from_type == to_type:
            return value

        source_members, result_members = members(from_type), members(to_type)
        if source_members is None or result_members is None or len(source_members) != len(result_members):
            return None

        parts = []
        for index, (source_member, result_member) in enumerate(zip(source_members, result_members)):
            out.append([(5 << 16) | OP_COMPOSITE_EXTRACT, source_member, bound, value, index])
            extracted = bound
            bound += 1
            part = rebuild(out, extracted, source_member, result_member)
            if part is None:
                return None

            parts.append(part)

        result = bound
        bound += 1
        out.append([((3 + len(parts)) << 16) | OP_COMPOSITE_CONSTRUCT, to_type, result, *parts])
        return result

    out = []
    left = 0
    for inst in instructions:
        if inst[0] & 0xFFFF != OP_COPY_LOGICAL:
            out.append(inst)
            continue

        result_type, result, source = inst[1], inst[2], inst[3]
        emitted = []
        value = rebuild(emitted, source, value_types.get(source), result_type) if source in value_types else None
        if value is None:
            out.append(inst)
            left += 1
            continue

        # Identical types copy as OpCopyObject; else the last construct has the copy's result type and takes its id.
        if not emitted:
            out.append([(4 << 16) | 83, result_type, result, source])
            continue

        emitted[-1][2] = result
        out.extend(emitted)

    header = header[:3] + [bound] + header[4:]
    return header, out, left


def unwrap_sampled_images(instructions):
    """naga has no OpImage. Splitting combined samplers turns a query such as TextureCube.GetDimensions into
    OpImage(OpSampledImage(image, sampler)): point the image operations that read it at the image itself, which naga
    traces back to its global. Returns the instructions and how many OpImage were left."""
    sources = {inst[2]: inst[3] for inst in instructions if inst[0] & 0xFFFF == OP_SAMPLED_IMAGE}
    unwrapped = {inst[2]: sources[inst[3]] for inst in instructions
                 if inst[0] & 0xFFFF == OP_IMAGE and inst[3] in sources}
    out = []
    for inst in instructions:
        op = inst[0] & 0xFFFF
        if op == OP_IMAGE and inst[2] in unwrapped:
            continue

        if op in IMAGE_OPERAND_OPS and inst[3] in unwrapped:
            inst = inst[:3] + [unwrapped[inst[3]]] + inst[4:]

        out.append(inst)

    left = sum(1 for inst in out if inst[0] & 0xFFFF == OP_IMAGE)
    return out, left


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

    header, instructions = read_instructions(legal.read_bytes())
    instructions, left = unwrap_sampled_images(instructions)
    if left:
        return f"{left} OpImage of a value that is not an OpSampledImage (naga cannot read them)"

    legal.write_bytes(write_instructions(header, instructions))
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
