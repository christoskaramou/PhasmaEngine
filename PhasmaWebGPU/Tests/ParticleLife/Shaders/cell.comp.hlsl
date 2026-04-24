// Port of gpu-life/src/countingSort/cell.wgsl.
// For each live particle, compute its grid cell hash, write a SortParticle
// entry {pos, vel, colour, idx = (cellHash, particleIndex)} into the sort
// output, and atomically bump counts[cellHash].
//
// Memory layout (32B per SortParticle, natural WGSL std430 rules):
//   +0  : pos.x (f32)
//   +4  : pos.y (f32)
//   +8  : vel.x (f32)
//   +12 : vel.y (f32)
//   +16 : colour (f32)
//   +20 : pad (4B so idx is 8-byte aligned)
//   +24 : idx.x (u32) = cellHash
//   +28 : idx.y (u32) = particleIndex

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

[[vk::binding(0, 0)]] cbuffer SimCB : register(b0, space0)
{
    Sim sim;
};

// Particle is 24B (6 dwords): pos(2f), vel(2f), colour(f), pad(f).
[[vk::binding(1, 0)]] ByteAddressBuffer inputParticles : register(t0, space0);

#define SORT_STRIDE 32u

[[vk::binding(2, 0)]] RWByteAddressBuffer outputParticles : register(u0, space0);

[[vk::binding(3, 0)]] RWStructuredBuffer<uint> counts : register(u1, space0);

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

static const uint kParticleCount = 50000u;

[numthreads(128, 1, 1)]
void CSMain(uint3 gid : SV_DispatchThreadID)
{
    uint i = gid.x;
    if (i >= kParticleCount)
        return;

    uint base = i * 24u;
    float px = asfloat(inputParticles.Load(base + 0u));
    float py = asfloat(inputParticles.Load(base + 4u));
    float vx = asfloat(inputParticles.Load(base + 8u));
    float vy = asfloat(inputParticles.Load(base + 12u));
    float cc = asfloat(inputParticles.Load(base + 16u));

    int gx = (int)floor(px / sim.cellSize);
    int gy = (int)floor(py / sim.cellSize);
    uint cellHash = hash3i(int3(gx, gy, 0)) % (uint)sim.cellAmt;

    uint outBase = i * SORT_STRIDE;
    outputParticles.Store(outBase + 0u, asuint(px));
    outputParticles.Store(outBase + 4u, asuint(py));
    outputParticles.Store(outBase + 8u, asuint(vx));
    outputParticles.Store(outBase + 12u, asuint(vy));
    outputParticles.Store(outBase + 16u, asuint(cc));
    // outBase + 20 is pad
    outputParticles.Store(outBase + 24u, cellHash);
    outputParticles.Store(outBase + 28u, i);

    uint unused;
    InterlockedAdd(counts[cellHash], 1u, unused);
}
