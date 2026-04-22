"""Extract stanfordDragon mesh from webgpu-samples TS source, run the
generateNormals algorithm + ground plane append exactly as the reference
sample's meshes/stanfordDragon.ts does, and emit a flat binary:

  uint32 vertexCount
  uint32 indexCount
  (float32 px, py, pz, nx, ny, nz) * vertexCount
  uint16 index * indexCount

The result is loaded by ShadowMappingTest.cpp at runtime.
"""

import json
import math
import struct
import sys
from pathlib import Path

SRC = Path(r"C:/Users/Christos/repos/webgpu-samples/meshes/stanfordDragonData.ts")
OUT = Path(__file__).resolve().parents[1] / "Assets" / "stanfordDragon.bin"


def extract_array(src: str, key: str) -> str:
    i = src.find(key + ":")
    if i < 0:
        raise RuntimeError(f"key {key} not found")
    j = src.find("[", i)
    depth = 0
    k = j
    while k < len(src):
        c = src[k]
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
            if depth == 0:
                return src[j : k + 1]
        k += 1
    raise RuntimeError(f"unbalanced brackets for {key}")


def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normalize(v):
    L = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if L == 0.0:
        return [0.0, 0.0, 0.0]
    return [v[0] / L, v[1] / L, v[2] / L]


def generate_normals(max_angle, positions, triangles):
    num_verts = len(positions)
    num_faces = len(triangles)

    face_normals = []
    for (a, b, c) in triangles:
        v1, v2, v3 = positions[a], positions[b], positions[c]
        face_normals.append(normalize(cross(sub(v2, v1), sub(v3, v1))))

    # dedup by position (JSON stringify in the reference, we use a tuple here)
    temp_verts = {}
    vert_indices = []
    for i in range(num_verts):
        key = tuple(positions[i])
        if key not in temp_verts:
            temp_verts[key] = len(temp_verts)
        vert_indices.append(temp_verts[key])

    vert_faces: dict[int, list[int]] = {}
    for i, tri in enumerate(triangles):
        for j in range(3):
            shared = vert_indices[tri[j]]
            vert_faces.setdefault(shared, []).append(i)

    max_angle_cos = math.cos(max_angle)
    new_positions = []
    new_normals = []
    new_verts: dict[tuple, int] = {}
    new_triangles: list[list[int]] = []

    for i, tri in enumerate(triangles):
        this_face = face_normals[i]
        out_tri = []
        for j in range(3):
            ndx = tri[j]
            shared = vert_indices[ndx]
            acc = [0.0, 0.0, 0.0]
            for face_ndx in vert_faces[shared]:
                other = face_normals[face_ndx]
                if dot(this_face, other) > max_angle_cos:
                    acc = add(acc, other)
            n = normalize(acc)
            key = (tuple(positions[ndx]), tuple(n))
            if key not in new_verts:
                new_verts[key] = len(new_positions)
                new_positions.append(positions[ndx])
                new_normals.append(n)
            out_tri.append(new_verts[key])
        new_triangles.append(out_tri)

    return new_positions, new_normals, new_triangles


def main() -> int:
    src = SRC.read_text()
    cells_txt = extract_array(src, "cells")
    positions_txt = extract_array(src, "positions")
    cells = json.loads(cells_txt)
    positions = json.loads(positions_txt)
    print(f"raw: {len(positions)} positions, {len(cells)} cells")

    pos, nrm, tri = generate_normals(math.pi, positions, cells)
    print(f"after generateNormals: {len(pos)} vertices, {len(tri)} triangles")

    base = len(pos)
    tri.append([base, base + 2, base + 1])
    tri.append([base, base + 1, base + 3])
    pos.extend([[-100, 20, -100], [100, 20, 100], [-100, 20, 100], [100, 20, -100]])
    nrm.extend([[0, 1, 0]] * 4)
    print(f"after ground plane: {len(pos)} vertices, {len(tri)} triangles")

    if len(pos) > 65535:
        print("ERROR: vertex count exceeds uint16", file=sys.stderr)
        return 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(struct.pack("<II", len(pos), len(tri) * 3))
        for p, n in zip(pos, nrm):
            f.write(struct.pack("<ffffff", p[0], p[1], p[2], n[0], n[1], n[2]))
        for t in tri:
            f.write(struct.pack("<HHH", t[0], t[1], t[2]))

    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
