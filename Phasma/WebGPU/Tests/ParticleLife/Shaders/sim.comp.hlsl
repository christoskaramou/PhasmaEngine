// Port of gpu-life/src/countingSort/sim.wgsl.
// Per-particle force accumulation + Euler integration using the counting-sort
// spatial grid. Reads the sorted particle list and per-cell indices to find
// neighbours inside 3x3 surrounding cells (extended as needed by rMax).

struct Uniforms
{
    float aspect;
    float _pad0;
    float _pad1;
    float _pad2;
    float4 mouse;
    float pointSize;
};

struct Sim
{
    float colours;
    float beta;
    float rMax;
    float force;
    float friction;
    float dt;
    float cellSize;
    float cellAmt;
    float avoidance;
    float worldSize;
    float border;
    float vortex;
};

#define SORT_STRIDE 32u
#define PARTICLE_STRIDE 24u

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0)
{
    Uniforms uniforms;
};
[[vk::binding(1, 0)]] cbuffer SimCB : register(b1, space0)
{
    Sim sim;
};

[[vk::binding(2, 0)]] StructuredBuffer<float> matrixBuf : register(t0, space0);

[[vk::binding(3, 0)]] ByteAddressBuffer inputParticles : register(t1, space0);
[[vk::binding(4, 0)]] RWByteAddressBuffer outputParticles : register(u0, space0);

[[vk::binding(5, 0)]] ByteAddressBuffer sorted : register(t2, space0);
[[vk::binding(6, 0)]] StructuredBuffer<uint2> indices : register(t3, space0);

static const uint kParticleCount = 50000u;

struct Particle
{
    float2 pos;
    float2 vel;
    float colour;
};

struct ForceOut
{
    float2 total;
    float2 avoid;
    float div;
};

Particle LoadParticle(uint i)
{
    uint base = i * PARTICLE_STRIDE;
    Particle p;
    p.pos.x = asfloat(inputParticles.Load(base + 0u));
    p.pos.y = asfloat(inputParticles.Load(base + 4u));
    p.vel.x = asfloat(inputParticles.Load(base + 8u));
    p.vel.y = asfloat(inputParticles.Load(base + 12u));
    p.colour = asfloat(inputParticles.Load(base + 16u));
    return p;
}

void StoreParticle(uint i, Particle p)
{
    uint base = i * PARTICLE_STRIDE;
    outputParticles.Store(base + 0u, asuint(p.pos.x));
    outputParticles.Store(base + 4u, asuint(p.pos.y));
    outputParticles.Store(base + 8u, asuint(p.vel.x));
    outputParticles.Store(base + 12u, asuint(p.vel.y));
    outputParticles.Store(base + 16u, asuint(p.colour));
    outputParticles.Store(base + 20u, 0u);
}

uint hash3i(int3 k)
{
    uint offset = 0x80000000u;
    uint x = ((uint)k.x + offset) * 0x9E3779B1u;
    uint y = ((uint)k.y + offset) * 0x85EBCA6Bu;
    uint z = ((uint)k.z + offset) * 0xC2B2AE35u;

    uint h = x ^ y ^ z;

    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;

    return h;
}

float2 forceCurve(float r, float a)
{
    float beta = sim.beta;
    if (r < beta)
    {
        return float2(0.0, r / beta - 1.0);
    }
    else if (beta < r && r < 1.0)
    {
        return float2(a * (1.0 - abs(2.0 * r - 1.0 - beta) / (1.0 - beta)), 0.0);
    }
    else
    {
        return float2(0.0, 0.0);
    }
}

ForceOut getCellForce(uint pi, Particle p, uint cell)
{
    float totalForceX = 0.0;
    float totalForceY = 0.0;
    float avoidForceX = 0.0;
    float avoidForceY = 0.0;
    float div = 0.0;

    float mrs = sim.rMax * sim.rMax;

    uint2 index = indices[cell];

    ForceOut outF;
    outF.total = float2(0.0, 0.0);
    outF.avoid = float2(0.0, 0.0);
    outF.div = 0.0;

    if (index.y == 0u)
        return outF;

    uint end = index.x + index.y;
    for (uint i = index.x; i < end; i++)
    {
        uint base = i * SORT_STRIDE;
        float ipx = asfloat(sorted.Load(base + 0u));
        float ipy = asfloat(sorted.Load(base + 4u));
        // vel at +8,+12 unused
        float icc = asfloat(sorted.Load(base + 16u));
        // pad at +20
        // idx.x at +24 (cellHash) unused here
        uint idxY = sorted.Load(base + 28u);

        if (idxY == pi)
            continue;

        float rx = ipx - p.pos.x;
        float ry = ipy - p.pos.y;
        float rs = rx * rx + ry * ry;

        if (rs > 0.0 && rs < mrs)
        {
            float r = sqrt(rs);
            float a = matrixBuf[(uint)p.colour * (uint)sim.colours + (uint)icc];
            float2 f = forceCurve(r / sim.rMax, a);

            totalForceX += rx / r * f.x;
            totalForceY += ry / r * f.x;

            avoidForceX += rx / r * f.y;
            avoidForceY += ry / r * f.y;

            div += max(0.0, -f.y) / 10.0 * sim.avoidance;
        }
        else if (rs == 0.0)
        {
            avoidForceX += fmod((float)pi * 20293.302, 4932.329) / 100000.0;
        }
    }

    outF.total = float2(totalForceX, totalForceY);
    outF.avoid = float2(avoidForceX, avoidForceY);
    outF.div = div;
    return outF;
}

float2 getForce(uint pi)
{
    Particle p = LoadParticle(pi);

    float2 totalForce = float2(0.0, 0.0);
    float2 avoidForce = float2(0.0, 0.0);
    float div = 0.0;

    int cxmin = (int)floor((p.pos.x - sim.rMax) / sim.cellSize);
    int cymin = (int)floor((p.pos.y - sim.rMax) / sim.cellSize);
    int cxmax = (int)floor((p.pos.x + sim.rMax) / sim.cellSize);
    int cymax = (int)floor((p.pos.y + sim.rMax) / sim.cellSize);

    for (int cx = cxmin; cx <= cxmax; cx++)
    {
        for (int cy = cymin; cy <= cymax; cy++)
        {
            uint c = hash3i(int3(cx, cy, 0)) % (uint)sim.cellAmt;
            ForceOut fc = getCellForce(pi, p, c);
            totalForce += fc.total;
            avoidForce += fc.avoid;
            div += fc.div;
        }
    }

    totalForce *= sim.rMax * sim.force / (1.0 + div);
    return totalForce + avoidForce;
}

[numthreads(128, 1, 1)]
void CSMain(uint3 gid : SV_DispatchThreadID)
{
    uint i = gid.x;
    if (i >= kParticleCount)
        return;

    Particle p = LoadParticle(i);

    float2 f = getForce(i);

    p.vel.x *= sim.friction;
    p.vel.y *= sim.friction;

    p.vel.x += f.x * sim.dt;
    p.vel.y += f.y * sim.dt;

    float d = sqrt(p.pos.x * p.pos.x + p.pos.y * p.pos.y) / sim.worldSize;

    if (sim.vortex > 0.0)
    {
        float colMat0 = matrixBuf[(uint)p.colour * 10u];
        float colMat1 = matrixBuf[(uint)p.colour];
        float colMat2 = matrixBuf[(uint)p.colour + 10u];

        p.vel.x -= p.pos.x * sim.dt * 5.0 * cos(d * 30.0 * p.pos.x * p.pos.y / colMat0) *
                   sin(p.pos.x * 10.0 * colMat1);
        p.vel.y -= p.pos.y * sim.dt * 5.0 * cos(d * 30.0 * p.pos.y * p.pos.x / colMat0) *
                   cos(p.pos.y * 10.0 * colMat1);

        p.vel.x -= p.pos.y * sim.dt * 5.0 * pow(d / 0.5, 2.0) * sin(d * 10.0 + colMat2);
        p.vel.y += p.pos.x * sim.dt * 5.0 * pow(d / 0.5, 2.0) * sin(d * 10.0 + colMat2);
    }

    if (uniforms.mouse.z != 0.0)
    {
        float dx = uniforms.mouse.x - p.pos.x;
        float dy = uniforms.mouse.y - p.pos.y;
        float md = sqrt(dx * dx + dy * dy);
        if (md < sim.rMax)
        {
            if (uniforms.mouse.z == 1.0)
            {
                p.vel.x += dx * 3.0;
                p.vel.y += dy * 3.0;
            }
            else
            {
                p.vel.x -= dx * 3.0;
                p.vel.y -= dy * 3.0;
            }
        }
    }

    if (sim.border > 0.0 && d > 0.9)
    {
        float bf = (d - 0.9) * 10.0;
        p.vel.x -= p.pos.x * bf;
        p.vel.y -= p.pos.y * bf;
    }

    p.pos.x += p.vel.x * sim.dt;
    p.pos.y += p.vel.y * sim.dt;

    StoreParticle(i, p);
}
