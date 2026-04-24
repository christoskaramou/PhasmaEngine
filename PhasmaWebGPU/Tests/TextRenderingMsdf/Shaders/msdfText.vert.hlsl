// MSDF text vertex shader — 1:1 port of msdfText.wgsl.
// Draws one triangle-strip quad per character (4 vertices, charCount instances).

struct CharInfo
{
    float2 texOffset;
    float2 texExtent;
    float2 size;
    float2 offset;
};

struct Camera
{
    column_major float4x4 projection;
    column_major float4x4 view;
};

struct FormattedTextHeader
{
    column_major float4x4 transform;
    float4 color;
    float scale;
    float _pad0;
    float _pad1;
    float _pad2;
};

// Font bindings (group 0).
[[vk::binding(2, 0)]] StructuredBuffer<CharInfo> chars : register(t2, space0);

// Text bindings (group 1).
[[vk::binding(0, 1)]] cbuffer CameraCB : register(b0, space1)
{
    Camera camera;
}

// The storage buffer layout (matches msdfText.wgsl's FormattedText):
//   [0 .. 15] : transform  (16 floats, column-major mat4x4)
//   [16..19]  : color      (vec4f)
//   [20]      : scale      (f32)
//   [21..23]  : padding    (vec3 alignment — the original leaves 3 floats before the array)
//   [24..]    : chars      : array<vec3f>  (x, y, charIndexAsFloat), 4 floats stride in the buffer
//
// With a plain structured buffer, the WGSL array<vec3f> is laid out at 16-byte
// stride, so each character entry reads as (x, y, charIndex, unused).
struct TextChar
{
    float2 offset;
    float charIndex;
    float _pad;
};

[[vk::binding(1, 1)]] ByteAddressBuffer textBuf : register(t1, space1);

static const float2 kQuadPos[4] =
    {
        float2(0.0f, -1.0f),
        float2(1.0f, -1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f)};

struct VSOutput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
    [[vk::location(1)]] float4 outColor : TEXCOORD1;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    // Header fields, read from the storage buffer byte-wise. The transform is
    // stored column-major (GLM convention on CPU, WGSL default for storage
    // buffers), so bytes 0..15 are column 0, 16..31 column 1, etc. HLSL's
    // float4x4(a,b,c,d) constructor treats the four args as ROWS, so feeding
    // columns in would transpose the matrix. Transpose to correct.
    float4 col0 = asfloat(textBuf.Load4(0));
    float4 col1 = asfloat(textBuf.Load4(16));
    float4 col2 = asfloat(textBuf.Load4(32));
    float4 col3 = asfloat(textBuf.Load4(48));
    float4x4 transformMat = transpose(float4x4(col0, col1, col2, col3));

    float4 textColor = asfloat(textBuf.Load4(64));
    float textScale = asfloat(textBuf.Load(80));

    // Character entries start at byte offset 96 (24 floats).
    // Each entry is a vec3f in WGSL which occupies 16 bytes (padding to vec4).
    uint charByteOffset = 96u + instanceId * 16u;
    float3 textElement = asfloat(textBuf.Load3(charByteOffset));

    uint charIndex = (uint)textElement.z;
    CharInfo c = chars[charIndex];

    float2 quad = kQuadPos[vertexId];
    float2 charPos = (quad * c.size + textElement.xy + c.offset) * textScale;

    float4 world = mul(transformMat, float4(charPos, 0.0f, 1.0f));
    float4 viewPos = mul(camera.view, world);
    float4 clipPos = mul(camera.projection, viewPos);

    // Vulkan Y-down NDC: CPU projection already flips Y, no extra flip here.
    VSOutput o;
    o.svPos = clipPos;

    float2 uv = quad * float2(1.0f, -1.0f);
    uv *= c.texExtent;
    uv += c.texOffset;
    o.texcoord = uv;
    o.outColor = textColor;
    return o;
}
