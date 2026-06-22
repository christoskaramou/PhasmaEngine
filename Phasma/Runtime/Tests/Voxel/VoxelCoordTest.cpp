// VoxelCoordTest.cpp
#include "Voxel/VoxelTypes.h"
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
    CHECK(WorldToColumn(0, 0).cx == 0 && WorldToColumn(0, 0).cz == 0);
    CHECK(WorldToColumn(15, 15).cx == 0 && WorldToColumn(15, 15).cz == 0);
    CHECK(WorldToColumn(16, -1).cx == 1 && WorldToColumn(16, -1).cz == -1); // floor-div for negatives
    CHECK(LocalX(-1) == 15 && LocalZ(-16) == 0);
    CHECK(SectionIndex(0) == 0 && SectionIndex(16) == 1 && LocalY(17) == 1);
    CHECK(BlockIndex(0, 0, 0) == 0 && BlockIndex(15, 15, 15) == kSectionDim * kSectionDim * kSectionDim - 1);
    if (g_fail == 0)
        printf("OK VoxelCoordTest\n");
    return g_fail;
}
