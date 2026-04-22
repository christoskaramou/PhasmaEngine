struct Particle
{
    float2 pos;
    float2 vel;
};

struct SimParams
{
    float deltaT;
    float rule1Distance;
    float rule2Distance;
    float rule3Distance;
    float rule1Scale;
    float rule2Scale;
    float rule3Scale;
};

[[vk::binding(0, 0)]] cbuffer ParamsCB : register(b0, space0) { SimParams params; }
[[vk::binding(1, 0)]] StructuredBuffer<Particle>   particlesA : register(t1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<Particle> particlesB : register(u2, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint index = dtid.x;

    uint numParticles, stride;
    particlesA.GetDimensions(numParticles, stride);
    if (index >= numParticles)
        return;

    float2 vPos = particlesA[index].pos;
    float2 vVel = particlesA[index].vel;
    float2 cMass = float2(0.0f, 0.0f);
    float2 cVel = float2(0.0f, 0.0f);
    float2 colVel = float2(0.0f, 0.0f);
    uint cMassCount = 0u;
    uint cVelCount = 0u;

    for (uint i = 0u; i < numParticles; i++)
    {
        if (i == index)
            continue;

        float2 pos = particlesA[i].pos;
        float2 vel = particlesA[i].vel;
        if (distance(pos, vPos) < params.rule1Distance)
        {
            cMass += pos;
            cMassCount++;
        }
        if (distance(pos, vPos) < params.rule2Distance)
        {
            colVel -= (pos - vPos);
        }
        if (distance(pos, vPos) < params.rule3Distance)
        {
            cVel += vel;
            cVelCount++;
        }
    }

    if (cMassCount > 0u)
        cMass = (cMass / float(cMassCount)) - vPos;
    if (cVelCount > 0u)
        cVel /= float(cVelCount);

    vVel += (cMass * params.rule1Scale) +
            (colVel * params.rule2Scale) +
            (cVel * params.rule3Scale);

    vVel = normalize(vVel) * clamp(length(vVel), 0.0f, 0.1f);
    vPos = vPos + (vVel * params.deltaT);

    if (vPos.x < -1.0f) vPos.x =  1.0f;
    if (vPos.x >  1.0f) vPos.x = -1.0f;
    if (vPos.y < -1.0f) vPos.y =  1.0f;
    if (vPos.y >  1.0f) vPos.y = -1.0f;

    Particle p;
    p.pos = vPos;
    p.vel = vVel;
    particlesB[index] = p;
}
