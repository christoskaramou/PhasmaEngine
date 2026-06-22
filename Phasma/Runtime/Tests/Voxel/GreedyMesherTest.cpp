#include "Voxel/GreedyMesher.h"
#include "Voxel/BlockRegistry.h"
#include <cstdio>
#include <cstring>
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
    BlockRegistry reg;
    reg.Register({1, "stone", true, true, VoxelRenderClass::Opaque, {0, 0, 0, 0, 0, 0}});
    GreedyMesher m;
    auto empty = m.Mesh([](int, int, int)
                        { return (BlockId)0; }, reg, 0);
    CHECK(empty.vertices.empty() && empty.indices.empty());
    auto one = m.Mesh([](int x, int y, int z)
                      { return (x == 0 && y == 0 && z == 0) ? (BlockId)1 : (BlockId)0; }, reg, 0);
    CHECK(one.vertices.size() == 24);
    CHECK(one.indices.size() == 36);
    bool seen[6] = {false, false, false, false, false, false};
    for (const auto &v : one.vertices)
    {
        if (v.normals[0] > 0.5f)
            seen[0] = true;
        if (v.normals[0] < -0.5f)
            seen[1] = true;
        if (v.normals[1] > 0.5f)
            seen[2] = true;
        if (v.normals[1] < -0.5f)
            seen[3] = true;
        if (v.normals[2] > 0.5f)
            seen[4] = true;
        if (v.normals[2] < -0.5f)
            seen[5] = true;
        uint32_t tile;
        memcpy(&tile, &v.joints[0], sizeof(uint32_t));
        CHECK(tile == 0);
    }
    for (int f = 0; f < 6; ++f)
        CHECK(seen[f]);
    auto solid = m.Mesh(
        [](int x, int y, int z)
        { return (x >= 0 && x < 16 && y >= 0 && y < 16 && z >= 0 && z < 16) ? (BlockId)1 : (BlockId)0; },
        reg, 0);
    CHECK(solid.vertices.size() == 24);
    CHECK(solid.indices.size() == 36);
    if (g_fail == 0)
        printf("OK GreedyMesherTest\n");
    return g_fail;
}
