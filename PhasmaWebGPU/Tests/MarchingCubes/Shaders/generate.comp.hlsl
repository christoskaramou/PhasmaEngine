struct GenerateUniforms
{
    float time;
    float unionSmoothness;
    float ringVariation;
    float rotationAngle;
};

struct SurfaceVertex
{
    float4 positionAO;
    float4 normalPad;
};

[[vk::binding(0, 0)]] cbuffer GenerateUniformsCB : register(b0, space0)
{
    GenerateUniforms uniforms;
};

[[vk::binding(1, 0)]] RWStructuredBuffer<SurfaceVertex> outVertices : register(u0, space0);

static const uint kGridDim = 32u;
static const uint kCells = kGridDim * kGridDim * kGridDim;
static const uint kVerticesPerTet = 6u;
static const uint kVerticesPerCell = 36u;
static const float kWorldExtent = 24.0f;
static const float kCellSize = (2.0f * kWorldExtent) / float(kGridDim);

static const uint4 kTets[6] = {
    uint4(0u, 5u, 1u, 6u),
    uint4(0u, 1u, 2u, 6u),
    uint4(0u, 2u, 3u, 6u),
    uint4(0u, 3u, 7u, 6u),
    uint4(0u, 7u, 4u, 6u),
    uint4(0u, 4u, 5u, 6u),
};

float3 CornerOffset(uint corner)
{
    return float3((corner == 1u || corner == 2u || corner == 5u || corner == 6u) ? 1.0f : 0.0f,
                  (corner == 2u || corner == 3u || corner == 6u || corner == 7u) ? 1.0f : 0.0f,
                  (corner >= 4u) ? 1.0f : 0.0f);
}

float3 RotateAxisAngle(float3 p, float3 axis, float angle)
{
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0f - c);
}

float SdRoundBox(float3 p, float3 b, float r)
{
    return length(max(abs(p) - b, float3(0.0f, 0.0f, 0.0f))) - r;
}

float SdTorus(float3 p, float2 t)
{
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float OpIntersection(float a, float b)
{
    return max(a, b);
}

float SmoothUnion(float a, float b, float k)
{
    k = max(k, 0.001f);
    float res = exp(-k * a) + exp(-k * b);
    return -log(max(res, 1.0e-6f)) / k;
}

float ComputeDensity(float3 p)
{
    float3 ws = p;
    float radius = 8.0f;

    float3 q = RotateAxisAngle(ws, float3(1.0f, 0.0f, 1.0f), 3.14159265f / 4.0f);
    float sineTime = sin(uniforms.time);
    float d = SdRoundBox(q, float3(radius, radius, radius), 0.125f * radius);

    float hs = 0.40f;
    float2 helix = float2(cos(hs * ws.y), sin(hs * ws.y));
    d += OpIntersection(dot(helix, hs * ws.xz), d);

    q = ws.yzx;
    q = RotateAxisAngle(q, float3(1.0f, 1.0f, 1.0f), uniforms.rotationAngle);
    d = SmoothUnion(d,
                    SdTorus(q, float2(2.1f * radius, 2.0f + uniforms.ringVariation * sineTime)),
                    1.0f - uniforms.unionSmoothness);
    return d;
}

float3 ComputeNormal(float3 p)
{
    float eps = 0.12f;
    float3 ex = float3(eps, 0.0f, 0.0f);
    float3 ey = float3(0.0f, eps, 0.0f);
    float3 ez = float3(0.0f, 0.0f, eps);
    float3 n = float3(ComputeDensity(p + ex) - ComputeDensity(p - ex),
                      ComputeDensity(p + ey) - ComputeDensity(p - ey),
                      ComputeDensity(p + ez) - ComputeDensity(p - ez));
    float len = length(n);
    return len > 0.00001f ? n / len : float3(0.0f, 1.0f, 0.0f);
}

float AmbientOcclusion(float3 p, float3 n)
{
    float ao = 0.0f;
    [unroll]
    for (uint i = 1u; i <= 5u; ++i)
    {
        float h = 0.55f * float(i);
        float d = ComputeDensity(p + n * h);
        ao += saturate((h - d) * 0.45f);
    }
    return saturate(1.0f - ao * 0.16f);
}

void StoreVertex(uint index, float3 position, float3 normal)
{
    SurfaceVertex v;
    v.positionAO = float4(position, AmbientOcclusion(position, normal));
    v.normalPad = float4(normal, 0.0f);
    outVertices[index] = v;
}

void StoreEmpty(uint index)
{
    SurfaceVertex v;
    v.positionAO = float4(0.0f, 0.0f, 0.0f, 1.0f);
    v.normalPad = float4(0.0f, 1.0f, 0.0f, 0.0f);
    outVertices[index] = v;
}

float3 IntersectEdge(float3 a, float da, float3 b, float db)
{
    float t = saturate(da / (da - db));
    return lerp(a, b, t);
}

void EmitTriangle(uint outBase, uint triangleIndex, float3 a, float3 b, float3 c)
{
    uint vertexBase = outBase + triangleIndex * 3u;
    StoreVertex(vertexBase + 0u, a, ComputeNormal(a));
    StoreVertex(vertexBase + 1u, b, ComputeNormal(b));
    StoreVertex(vertexBase + 2u, c, ComputeNormal(c));
}

void EmitTet(uint outBase, float3 p[4], float d[4])
{
    [unroll]
    for (uint i = 0u; i < kVerticesPerTet; ++i)
    {
        StoreEmpty(outBase + i);
    }

    uint inside[4];
    uint outside[4];
    uint insideCount = 0u;
    uint outsideCount = 0u;

    for (uint i = 0u; i < 4u; ++i)
    {
        if (d[i] < 0.0f)
            inside[insideCount++] = i;
        else
            outside[outsideCount++] = i;
    }

    if (insideCount == 0u || insideCount == 4u)
        return;

    if (insideCount == 1u)
    {
        uint i0 = inside[0];
        EmitTriangle(outBase, 0u,
                     IntersectEdge(p[i0], d[i0], p[outside[0]], d[outside[0]]),
                     IntersectEdge(p[i0], d[i0], p[outside[1]], d[outside[1]]),
                     IntersectEdge(p[i0], d[i0], p[outside[2]], d[outside[2]]));
        return;
    }

    if (insideCount == 3u)
    {
        uint o0 = outside[0];
        EmitTriangle(outBase, 0u,
                     IntersectEdge(p[o0], d[o0], p[inside[2]], d[inside[2]]),
                     IntersectEdge(p[o0], d[o0], p[inside[1]], d[inside[1]]),
                     IntersectEdge(p[o0], d[o0], p[inside[0]], d[inside[0]]));
        return;
    }

    uint i0 = inside[0];
    uint i1 = inside[1];
    uint o0 = outside[0];
    uint o1 = outside[1];
    float3 a = IntersectEdge(p[i0], d[i0], p[o0], d[o0]);
    float3 b = IntersectEdge(p[i1], d[i1], p[o0], d[o0]);
    float3 c = IntersectEdge(p[i1], d[i1], p[o1], d[o1]);
    float3 q = IntersectEdge(p[i0], d[i0], p[o1], d[o1]);
    EmitTriangle(outBase, 0u, a, b, c);
    EmitTriangle(outBase, 1u, a, c, q);
}

[numthreads(128, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint cellId = dtid.x;
    if (cellId >= kCells)
        return;

    uint x = cellId % kGridDim;
    uint y = (cellId / kGridDim) % kGridDim;
    uint z = cellId / (kGridDim * kGridDim);

    float3 cellMin = (float3(x, y, z) / float(kGridDim)) * (2.0f * kWorldExtent) -
                     float3(kWorldExtent, kWorldExtent, kWorldExtent);

    float3 corners[8];
    float density[8];
    [unroll]
    for (uint i = 0u; i < 8u; ++i)
    {
        corners[i] = cellMin + CornerOffset(i) * kCellSize;
        density[i] = ComputeDensity(corners[i]);
    }

    uint cellBase = cellId * kVerticesPerCell;
    [unroll]
    for (uint t = 0u; t < 6u; ++t)
    {
        uint4 ids = kTets[t];
        float3 p[4] = {corners[ids.x], corners[ids.y], corners[ids.z], corners[ids.w]};
        float d[4] = {density[ids.x], density[ids.y], density[ids.z], density[ids.w]};
        EmitTet(cellBase + t * kVerticesPerTet, p, d);
    }
}
