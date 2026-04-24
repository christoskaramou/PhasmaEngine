// Port of webgpu-samples/alpha-to-coverage-emulator scenes/FoliageCommon.wgsl
// (vmainFoliage entry) to HLSL (SM 6.0) for PhasmaWebGPU's DXC->SPIR-V path.
// Renders 1000 instances of feathered "leaves" arranged roughly in a sphere.
// The upstream shader draws alpha-masked quads. This port emits a small disk
// mesh per instance so transparent quad corners cannot leak through on native
// A2C paths that mishandle fragment alpha coverage.

cbuffer Uniforms : register(b0, space0)
{
    column_major float4x4 uViewProj;
    float                 uFeatheringWidthPx;
    float3                uPad0;
};

struct Varying
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float2 gb  : TEXCOORD1;
};

static const float kPi  = 3.14159265359f;
static const float kPhi = 1.618033988749f;

static const uint kDiskSegmentCount = 32u;

float2 DiskVertex(uint vertexIndex)
{
    uint segment = vertexIndex / 3u;
    uint corner = vertexIndex % 3u;

    if (corner == 0u)
        return float2(0.0f, 0.0f);

    float edge = (corner == 1u) ? 0.0f : 1.0f;
    float angle = (float(segment) + edge) * (2.0f * kPi / float(kDiskSegmentCount));
    return float2(cos(angle), sin(angle));
}

// Low-effort rotation matrix, identical to WGSL source.
float3x3 Rotate(float a, float b, float c)
{
    float t = fmod(a, 2.0f * kPi);
    float u = fmod(b, kPi * 0.5f);
    float v = fmod(c, kPi * 0.5f) - kPi * 0.25f;

    // WGSL matN constructors list columns; HLSL matrices here are treated as
    // row-major via HLSL default but we build rotation the same way as the
    // source by composing three matrices. Column-major vs row-major only
    // affects the layout of the multiply below. We use row-vector convention:
    //   v' = v * M
    // mirroring the WGSL (M * v). To keep the same visual result we simply
    // mirror the order and transpose each matrix.
    //
    // For clarity: build matrices in column-major (like WGSL), and multiply
    // as M3 * M2 * M1 * vec. Because HLSL matrices default to column-major
    // when constructed with `float3x3(col0, col1, col2)` via explicit columns,
    // we instead write them as the transposed form.

    // 3. rotate around Y by t (0..360deg)
    float3x3 R3 = float3x3(
         cos(t), 0.0f,  sin(t),
         0.0f,   1.0f,  0.0f,
        -sin(t), 0.0f,  cos(t)
    );
    // 2. rotate toward horizon by u (0..90deg)
    float3x3 R2 = float3x3(
         cos(u), -sin(u), 0.0f,
         sin(u),  cos(u), 0.0f,
         0.0f,    0.0f,   1.0f
    );
    // 1. rotate on axis by v (-45..45deg)
    float3x3 R1 = float3x3(
         cos(v), 0.0f,  sin(v),
         0.0f,   1.0f,  0.0f,
        -sin(v), 0.0f,  cos(v)
    );

    return mul(R3, mul(R2, R1));
}

Varying VSMain(uint vertex_index : SV_VertexID, uint instance_index : SV_InstanceID)
{
    float2 uv = DiskVertex(vertex_index);
    float3 leafSpacePos = float3(uv * 0.25f, 0.0f);

    float t = kPhi * (float)instance_index;

    float3 base = leafSpacePos + float3(0.0f, fmod(t, 5.0f), 0.0f);
    float3 worldSpacePos = mul(Rotate(t * 3.0f, t * 7.0f, t * 11.0f), base);

    // Green/blue lookup table identical to WGSL source.
    float2 gbTable[8] = {
        float2(0.3f, 0.3f),
        float2(0.8f, 0.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 0.5f),
        float2(1.0f, 1.0f),
        float2(0.5f, 1.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 0.8f),
    };
    uint idx = (uint)fmod(t * 41.0f, 8.0f);

    Varying o;
    o.pos = mul(uViewProj, float4(worldSpacePos, 1.0f));
    o.uv  = uv;
    o.gb  = gbTable[idx];
    return o;
}
