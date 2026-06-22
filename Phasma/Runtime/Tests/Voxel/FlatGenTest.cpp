#include "Voxel/FlatGen.h"
#include "Voxel/ChunkColumn.h"
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
    ChunkColumn col({0, 0});
    FlatGen gen(8, 1);
    gen.Generate(col);
    CHECK(col.GetLocal(5, 7, 5) == 1);
    CHECK(col.GetLocal(5, 8, 5) == kAir);
    CHECK(col.GetLocal(0, 0, 0) == 1);
    CHECK(col.GetLocal(15, 7, 15) == 1);
    CHECK(col.GetLocal(15, 8, 15) == kAir);
    if (g_fail == 0)
        printf("OK FlatGenTest\n");
    return g_fail;
}
