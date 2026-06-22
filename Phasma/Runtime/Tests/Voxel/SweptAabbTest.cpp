#include "Voxel/VoxelCollider.h"
#include <cstdio>
using namespace pe::voxel;
static int g_fail = 0;
static bool nearly(float a, float b)
{
    float d = a - b;
    return d < 0.02f && d > -0.02f;
}
#define CHECK(c)                                               \
    do                                                         \
    {                                                          \
        if (!(c))                                              \
        {                                                      \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                          \
        }                                                      \
    }                                                          \
    while (0)
#undef main // engine PCH pulls SDL.h (#define main SDL_main); undo it for this console test
int main()
{
    // Floor: cells with y<=0 solid; top surface of block y=0 is world y=1.
    auto floorSolid = [](int x, int y, int z)
    { return y <= 0; };
    vec3 half(0.4f, 0.9f, 0.4f);
    vec3 r = MoveAabb(vec3(0.5f, 2.0f, 0.5f), half, vec3(0, -5, 0), floorSolid);
    CHECK(nearly(r.y - half.y, 1.0f)); // bottom rests on y=1
    CHECK(nearly(r.x, 0.5f) && nearly(r.z, 0.5f));
    // Wall: cells with x>=3 solid; near face at world x=3.
    auto wallSolid = [](int x, int y, int z)
    { return x >= 3; };
    vec3 r2 = MoveAabb(vec3(1.0f, 5.0f, 1.0f), half, vec3(5, 0, 0), wallSolid);
    CHECK(nearly(r2.x + half.x, 3.0f)); // right face abuts plane x=3
    CHECK(nearly(r2.y, 5.0f) && nearly(r2.z, 1.0f));
    // Corner: two perpendicular walls; body wedges into the x=3,z=3 corner (Y->X->Z order).
    auto cornerSolid = [](int x, int y, int z)
    { return x >= 3 || z >= 3; };
    vec3 r3 = MoveAabb(vec3(1.0f, 5.0f, 1.0f), half, vec3(5, 0, 5), cornerSolid);
    CHECK(nearly(r3.x + half.x, 3.0f));
    CHECK(nearly(r3.z + half.z, 3.0f));
    if (g_fail == 0)
        printf("OK SweptAabbTest\n");
    return g_fail;
}
