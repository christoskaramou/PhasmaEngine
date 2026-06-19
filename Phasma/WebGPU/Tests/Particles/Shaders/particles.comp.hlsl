struct SimulationParams
{
    float deltaTime;
    float brightnessFactor;
    uint  numParticles;
    uint  _pad;
    uint4 seed;
};

struct Particle
{
    float3 position;
    float  lifetime;
    float4 color;
    float3 velocity;
    float  _pad;
};

[[vk::binding(0, 0)]] cbuffer SimParams : register(b0, space0)
{
    SimulationParams sim_params;
}

[[vk::binding(1, 0)]] RWStructuredBuffer<Particle> particles : register(u0, space0);
[[vk::binding(2, 0)]] Texture2D<float4>            tex       : register(t0, space0);

static uint4 rnd;

void init_rand(uint invocation_id, uint4 seed)
{
    static const uint4 A = uint4(1741651u * 1009u,
                                 140893u  * 1609u * 13u,
                                 6521u    * 983u  * 7u * 2u,
                                 1109u    * 509u  * 83u * 11u * 3u);
    rnd = (A * uint4(invocation_id, invocation_id, invocation_id, invocation_id)) ^ seed;
}

float rand_f()
{
    static const uint4 C = uint4(60493u  * 9377u,
                                 11279u  * 2539u * 23u,
                                 7919u   * 631u  * 5u * 3u,
                                 1277u   * 211u  * 19u * 7u * 2u);
    rnd  = (rnd * C) ^ (rnd.yzwx >> 4u);
    return float(rnd.x ^ rnd.y) / float(0xffffffffu);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    if (idx >= sim_params.numParticles)
        return;

    init_rand(idx, sim_params.seed);

    Particle p = particles[idx];

    p.velocity.z -= sim_params.deltaTime * 0.5f;
    p.position   += sim_params.deltaTime * p.velocity;
    p.lifetime   -= sim_params.deltaTime;
    p.color.a     = smoothstep(0.0f, 0.5f, p.lifetime);

    if (p.lifetime < 0.0f)
    {
        uint width, height, numMips;
        tex.GetDimensions(0, width, height, numMips);

        int2 coord = int2(0, 0);
        for (uint level = numMips - 1u; level > 0u; level--)
        {
            float4 prob = tex.Load(int3(coord, (int)level));
            float  v    = rand_f();

            bool4 mask;
            mask.x = (v >= 0.0f)   && (v < prob.r);
            mask.y = (v >= prob.r)  && (v < prob.g);
            mask.z = (v >= prob.g)  && (v < prob.b);
            mask.w = (v >= prob.b)  && (v < prob.a);

            coord *= 2;
            coord.x += (mask.y || mask.w) ? 1 : 0;
            coord.y += (mask.z || mask.w) ? 1 : 0;
        }

        float2 uv   = float2(coord) / float2(width, height);
        p.position  = float3((uv - 0.5f) * 3.0f * float2(1.0f, -1.0f), 0.0f);
        p.color     = tex.Load(int3(coord, 0));
        p.color.rgb *= sim_params.brightnessFactor;

        p.velocity.x = (rand_f() - 0.5f) * 0.1f;
        p.velocity.y = (rand_f() - 0.5f) * 0.1f;
        p.velocity.z =  rand_f()          * 0.3f;
        p.lifetime   =  0.5f + rand_f()  * 3.0f;
    }

    particles[idx] = p;
}
