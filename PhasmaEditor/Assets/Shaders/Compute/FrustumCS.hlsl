struct Plane
{
    float3 normal;
    float d;
};

struct Plane6
{
    Plane plane[6];
};

[[vk::binding(0, 0)]] StructuredBuffer<float4x4> DataIn;
[[vk::binding(1, 0)]] RWStructuredBuffer<Plane6> DataOut;

[numthreads(1, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    // transpose just to make the calculations look simpler
    float4x4 pvm = transpose(DataIn[DTid.x]);
    Plane6 p6;

    /* Extract the numbers for the RIGHT plane */
    float4 temp = pvm[3] - pvm[0];
    temp /= length(temp.xyz);

    p6.plane[0].normal = temp.xyz;
    p6.plane[0].d = temp.w;

    /* Extract the numbers for the LEFT plane */
    temp = pvm[3] + pvm[0];
    temp /= length(temp.xyz);

    p6.plane[1].normal = temp.xyz;
    p6.plane[1].d = temp.w;

    /* Extract the BOTTOM plane */
    temp = pvm[3] - pvm[1];
    temp /= length(temp.xyz);

    p6.plane[2].normal = temp.xyz;
    p6.plane[2].d = temp.w;

    /* Extract the TOP plane */
    temp = pvm[3] + pvm[1];
    temp /= length(temp.xyz);

    p6.plane[3].normal = temp.xyz;
    p6.plane[3].d = temp.w;

    /* Extract the FAR plane */
    temp = pvm[3] - pvm[2];
    temp /= length(temp.xyz);

    p6.plane[4].normal = temp.xyz;
    p6.plane[4].d = temp.w;

    /* Extract the NEAR plane */
    temp = pvm[3] + pvm[2];
    temp /= length(temp.xyz);

    p6.plane[5].normal = temp.xyz;
    p6.plane[5].d = temp.w;

    DataOut[DTid.x] = p6;
}
