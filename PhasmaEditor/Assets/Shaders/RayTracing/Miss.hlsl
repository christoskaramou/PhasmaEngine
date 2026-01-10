struct HitPayload
{
    float4 color;
};

[shader("miss")]
void main(inout HitPayload payload)
{
    payload.color = float4(0.0, 0.0, 0.2, 1.0); // Dark blue background
}
