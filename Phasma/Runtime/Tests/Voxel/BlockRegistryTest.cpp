#include "Voxel/BlockRegistry.h"
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
    BlockRegistry reg;
    CHECK(reg.Count() == 1); // air auto-registered
    CHECK(reg.IsSolid(0) == false && reg.IsOpaque(0) == false);
    BlockId stone = reg.Register({1, "stone", true, true, VoxelRenderClass::Opaque, {3, 3, 3, 3, 3, 3}});
    CHECK(stone == 1);
    CHECK(reg.IsSolid(1) == true && reg.IsOpaque(1) == true);
    CHECK(reg.FaceTile(1, 0) == 3);
    CHECK(reg.Get(1).name == "stone");
    CHECK(reg.Count() == 2);
    if (g_fail == 0)
        printf("OK BlockRegistryTest\n");
    return g_fail;
}
