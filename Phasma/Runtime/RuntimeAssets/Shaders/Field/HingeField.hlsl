// HingeField.hlsl - shared by the hinge field's G-buffer and shadow shaders (see Scene/HingeField.h).
//
// Every instance is one small box standing on the ground. It has no vertex data: its shape comes from SV_VertexID
// and its pose from one 32-byte record. Once told when to fall, the box animates itself from the clock, so standing
// and falling boxes cost the CPU nothing per frame.
#ifndef HINGE_FIELD_HLSL
#define HINGE_FIELD_HLSL

struct HingeInstance
{
    float x, y, z, yaw;
    float tStart, tContact;
    uint color; // RGBA8
    uint state; // contact 8 | final 8 | generation 14 | backward 1 | hidden 1
};

static const float kHingeHeight = 1.0f;
static const float kHingeWidth = 0.5f;
static const float kHingeThickness = 0.15f;
static const float3 kHingeHalf = float3(kHingeThickness * 0.5f, kHingeHeight * 0.5f, kHingeWidth * 0.5f);
static const float kHingePi = 3.14159265f;
static const float kHingeSettle = 0.24f;
static const float kHingeFlat = 1.55858f; // 89.3 degrees: lying on the floor
static const int kHingeLeanSteps = 10;

static const float3 kHingeN[6] = { float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
                                   float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1) };
static const float3 kHingeU[6] = { float3(0, 1, 0), float3(0, 0, 1), float3(0, 0, 1),
                                   float3(1, 0, 0), float3(1, 0, 0), float3(0, 1, 0) };
static const float3 kHingeV[6] = { float3(0, 0, 1), float3(0, 1, 0), float3(1, 0, 0),
                                   float3(0, 0, 1), float3(0, 1, 0), float3(1, 0, 0) };
static const float2 kHingeCorner[6] = { float2(-1, -1), float2(1, -1), float2(1, 1),
                                        float2(-1, -1), float2(1, 1), float2(-1, 1) };

bool HingeHidden(uint state) { return (state >> 31) != 0u; }
float HingeSide(uint state) { return ((state >> 30) & 1u) != 0u ? -1.0f : 1.0f; }

// How far a box can tilt while the next one, S ahead pivot to pivot, stands at tilt `next`: the first of its top edge
// coming down on the next one's back, or its front face on either of that one's back corners. Nothing met: it lies flat.
float HingeLean(float next, float S)
{
    const float c = cos(next), s = sin(next);
    const float h2 = kHingeHeight * kHingeHeight;
    float best = kHingeFlat;
    const float a = next + asin(clamp((S * c - kHingeThickness) / kHingeHeight, -1.0f, 1.0f));
    const float along = kHingeHeight * cos(a - next) - S * s;
    if (along >= 0.0f && along <= kHingeHeight)
        best = min(best, a);
    for (int k = 0; k < 2; ++k)
    {
        const float up = float(k) * kHingeHeight;
        const float bx = S - kHingeThickness * c + up * s, bz = kHingeThickness * s + up * c;
        if (bx * bx + bz * bz <= h2)
            best = min(best, atan2(bx, bz));
    }
    return best;
}

// The box's tilt at this time, in radians. An instance from an earlier generation is standing again.
float HingeAngle(HingeInstance inst, float time, uint generation)
{
    if (((inst.state >> 16) & 0x3FFFu) != generation || time <= inst.tStart)
        return 0.0f;

    const float contact = float(inst.state & 0xFFu) / 255.0f * kHingePi * 0.5f;
    const float final = float((inst.state >> 8) & 0xFFu) / 255.0f * kHingePi * 0.5f;
    const float span = max(1e-3f, inst.tContact - inst.tStart);
    if (time < inst.tContact)
        return contact * pow((time - inst.tStart) / span, 1.6f);

    // In a row each box leans on the next, and the next is doing what this one did one step earlier: so its tilt is
    // the fall so far, passed back through the contact once per box ahead. The record says where it ends up.
    if (final - contact > 0.01f && final < 1.48353f)
    {
        const float steps = (time - inst.tContact) / span;
        if (steps >= float(kHingeLeanSteps))
            return final;
        const float S = kHingeHeight * sin(contact) + kHingeThickness;
        const float row = S * S >= kHingeHeight * kHingeHeight + kHingeThickness * kHingeThickness
            ? kHingeFlat : acos(kHingeThickness / S);
        const int n = int(steps);
        float lean = contact * pow(steps - float(n), 1.6f);
        for (int i = 0; i <= n; ++i)
            lean = HingeLean(lean, S);
        return min(final, contact + (lean - contact) * (final - contact) / max(row - contact, 0.01f));
    }

    const float u = (time - inst.tContact) / kHingeSettle;
    // Starts gently: it can only lean further as fast as the box it struck gets out of the way.
    const float s = saturate(u);
    float angle = contact + (final - contact) * s * s * (3.0f - 2.0f * s);
    if (final >= 1.48353f && u < 1.6f) // lands flat: a small bounce
    {
        const float k = saturate(u / 1.6f);
        angle -= 0.0872665f * sin(k * kHingePi) * (1.0f - k);
    }
    return angle;
}

// A local point (or, with hinge = 0, a direction) tipped about the bottom edge on the side the box falls toward.
float3 HingeTip(float3 p, float side, float hinge, float cosA, float sinA)
{
    const float x = p.x - side * hinge;
    return float3(x * cosA + side * p.y * sinA + side * hinge, -side * x * sinA + p.y * cosA, p.z);
}

float3 HingeYaw(float3 p, float cosY, float sinY)
{
    return float3(p.x * cosY + p.z * sinY, p.y, -p.x * sinY + p.z * cosY);
}

// Where a local point ends up in the world at this tilt.
float3 HingeWorld(HingeInstance inst, float3 local, float side, float angle, float cosY, float sinY)
{
    const float3 tipped = HingeTip(local, side, kHingeThickness * 0.5f, cos(angle), sin(angle));
    return HingeYaw(tipped, cosY, sinY) + float3(inst.x, inst.y, inst.z);
}

float3 HingeCornerLocal(uint face, uint corner)
{
    const float2 c = kHingeCorner[corner];
    return (kHingeN[face] + kHingeU[face] * c.x + kHingeV[face] * c.y) * kHingeHalf + float3(0.0f, kHingeHalf.y, 0.0f);
}

// Far boxes are three quads, the most a box can show: for each axis, the face turned toward the eye. So at the switch
// from whole boxes nothing visible changes. eyeLocal: the eye from the box's base, yawed but not tipped.
uint HingeFarFace(uint quad, float3 eyeLocal, float side, float cosA, float sinA)
{
    const float hinge = side * kHingeHalf.x;
    const float x = eyeLocal.x - hinge;
    const float s = side * sinA;
    if (quad == 0u)
        return x * cosA - eyeLocal.y * s + hinge >= 0.0f ? 0u : 1u;
    if (quad == 1u)
        return x * s + eyeLocal.y * cosA - kHingeHalf.y >= 0.0f ? 2u : 3u;
    return eyeLocal.z >= 0.0f ? 4u : 5u;
}

#endif
