#include "Voxel/VoxelCollider.h"
#include <cstdio>
using namespace pe::voxel;
static int g_fail = 0;
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
    auto isSolid = [](int x, int y, int z)
    { return x == 5 && y == 0 && z == 0; };
    auto h = RaycastVoxels(vec3(0.5f, 0.5f, 0.5f), vec3(1, 0, 0), 10.0f, isSolid);
    CHECK(h.hit);
    CHECK(h.cell.x == 5 && h.cell.y == 0 && h.cell.z == 0);
    CHECK(h.adjacent.x == 4 && h.adjacent.y == 0 && h.adjacent.z == 0);
    CHECK(h.normal.x < -0.5f); // points back toward -x (ray origin side)
    CHECK(h.normal.y > -0.5f && h.normal.y < 0.5f);
    CHECK(h.normal.z > -0.5f && h.normal.z < 0.5f);
    auto h2 = RaycastVoxels(vec3(0.5f, 0.5f, 0.5f), vec3(-1, 0, 0), 10.0f, isSolid);
    CHECK(!h2.hit);
    auto h3 = RaycastVoxels(vec3(0.5f, 0.5f, 0.5f), vec3(1, 0, 0), 2.0f, isSolid); // hit ~4.5 away
    CHECK(!h3.hit);
    if (g_fail == 0)
        printf("OK RaycastDdaTest\n");
    return g_fail;
}
