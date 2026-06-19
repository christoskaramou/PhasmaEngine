// One-shot asset exporter: reads webgpu-samples' stanfordDragonData.ts,
// runs the same generateNormals+computeProjectedPlaneUVs pipeline, appends
// the same ground plane, then writes a compact binary mesh file next to
// this script (../Assets/stanfordDragon.bin).
//
// Binary layout (little-endian):
//   u32 numVerts
//   u32 numTris
//   vert[numVerts] { f32 px, py, pz, f32 nx, ny, nz, f32 u, v }
//   tri [numTris] { u32 i0, i1, i2 }
//
// Usage: node export_dragon.mjs <path-to-webgpu-samples>
//   default: ../../../../webgpu-samples (sibling of PhasmaEngine)

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const samplesRoot = process.argv[2] ?? path.resolve(here, '../../../../../webgpu-samples');
const dataPath = path.join(samplesRoot, 'meshes/stanfordDragonData.ts');

const src = fs.readFileSync(dataPath, 'utf8');

function extractArray(name) {
    const start = src.indexOf(name + ':');
    if (start < 0) throw new Error(`Could not find ${name}: in ${dataPath}`);
    const bracketStart = src.indexOf('[', start);
    let depth = 0;
    let i = bracketStart;
    for (; i < src.length; i++) {
        const c = src[i];
        if (c === '[') depth++;
        else if (c === ']') { depth--; if (depth === 0) { i++; break; } }
    }
    return JSON.parse(src.slice(bracketStart, i));
}

const cells = extractArray('cells');
const positions = extractArray('positions');
console.log(`[dragon] parsed ${positions.length} raw positions, ${cells.length} cells`);

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const normalize = (v) => {
    const l = Math.hypot(v[0], v[1], v[2]) || 1;
    return [v[0] / l, v[1] / l, v[2] / l];
};

// Port of meshes/utils.ts generateNormals (maxAngle = PI → always merge).
function generateNormals(maxAngle, positions, triangles) {
    const numFaces = triangles.length;
    const faceNormals = triangles.map(([i0, i1, i2]) => {
        const a = positions[i0], b = positions[i1], c = positions[i2];
        return normalize(cross(sub(b, a), sub(c, a)));
    });

    // Shared-vertex map keyed on exact position.
    const sharedIdx = {};
    let sharedNext = 0;
    const shared = new Array(positions.length);
    for (let i = 0; i < positions.length; i++) {
        const key = positions[i].join(',');
        if (sharedIdx[key] === undefined) sharedIdx[key] = sharedNext++;
        shared[i] = sharedIdx[key];
    }

    // For each shared position, list of face indices that touch it.
    const vertFaces = [];
    for (let f = 0; f < numFaces; f++) {
        for (let v = 0; v < 3; v++) {
            const s = shared[triangles[f][v]];
            (vertFaces[s] ??= []).push(f);
        }
    }

    const newPositions = [];
    const newNormals = [];
    const vertDedup = {};
    let vertNext = 0;
    const getNew = (pos, norm) => {
        const key = pos.join(',') + '|' + norm.join(',');
        if (vertDedup[key] !== undefined) return vertDedup[key];
        const idx = vertNext++;
        vertDedup[key] = idx;
        newPositions.push(pos);
        newNormals.push(norm);
        return idx;
    };

    const cosMax = Math.cos(maxAngle);
    const newTriangles = [];
    for (let f = 0; f < numFaces; f++) {
        const tn = faceNormals[f];
        const tri = [];
        for (let v = 0; v < 3; v++) {
            const origIdx = triangles[f][v];
            const s = shared[origIdx];
            let acc = [0, 0, 0];
            for (const otherF of vertFaces[s]) {
                if (dot(tn, faceNormals[otherF]) > cosMax) acc = add(acc, faceNormals[otherF]);
            }
            tri.push(getNew(positions[origIdx], normalize(acc)));
        }
        newTriangles.push(tri);
    }

    return { positions: newPositions, normals: newNormals, triangles: newTriangles };
}

const gen = generateNormals(Math.PI, positions, cells);

// xy-projected UVs, then re-scaled to [0,1] per axis.
const uvs = gen.positions.map((p) => [p[0], p[1]]);
let uMin = Infinity, vMin = Infinity, uMax = -Infinity, vMax = -Infinity;
for (const [u, v] of uvs) {
    if (u < uMin) uMin = u;
    if (u > uMax) uMax = u;
    if (v < vMin) vMin = v;
    if (v > vMax) vMax = v;
}
for (const uv of uvs) {
    uv[0] = (uv[0] - uMin) / (uMax - uMin);
    uv[1] = (uv[1] - vMin) / (vMax - vMin);
}

// Append ground plane (matching stanfordDragon.ts exactly).
const baseIdx = gen.positions.length;
gen.triangles.push([baseIdx, baseIdx + 2, baseIdx + 1], [baseIdx, baseIdx + 1, baseIdx + 3]);
gen.positions.push([-100, 20, -100], [100, 20, 100], [-100, 20, 100], [100, 20, -100]);
gen.normals.push([0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0]);
uvs.push([0, 0], [1, 1], [0, 1], [1, 0]);

const numVerts = gen.positions.length;
const numTris = gen.triangles.length;
console.log(`[dragon] emitted ${numVerts} verts, ${numTris} tris`);

const headerBytes = 8;
const vertBytes = numVerts * 32;
const triBytes = numTris * 12;
const buf = Buffer.alloc(headerBytes + vertBytes + triBytes);
buf.writeUInt32LE(numVerts, 0);
buf.writeUInt32LE(numTris, 4);
let o = headerBytes;
for (let i = 0; i < numVerts; i++) {
    const p = gen.positions[i], n = gen.normals[i], uv = uvs[i];
    buf.writeFloatLE(p[0], o); buf.writeFloatLE(p[1], o + 4); buf.writeFloatLE(p[2], o + 8);
    buf.writeFloatLE(n[0], o + 12); buf.writeFloatLE(n[1], o + 16); buf.writeFloatLE(n[2], o + 20);
    buf.writeFloatLE(uv[0], o + 24); buf.writeFloatLE(uv[1], o + 28);
    o += 32;
}
for (let i = 0; i < numTris; i++) {
    const t = gen.triangles[i];
    buf.writeUInt32LE(t[0], o); buf.writeUInt32LE(t[1], o + 4); buf.writeUInt32LE(t[2], o + 8);
    o += 12;
}

const outDir = path.resolve(here, '../Assets');
fs.mkdirSync(outDir, { recursive: true });
const outPath = path.join(outDir, 'stanfordDragon.bin');
fs.writeFileSync(outPath, buf);
console.log(`[dragon] wrote ${outPath} (${buf.length} bytes)`);
