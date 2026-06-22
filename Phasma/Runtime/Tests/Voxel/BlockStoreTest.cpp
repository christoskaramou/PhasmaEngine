#include "Voxel/ChunkSection.h"
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
    ChunkSection s;
    CHECK(s.IsEmpty());
    s.Set(1, 2, 3, 7);
    CHECK(s.Get(1, 2, 3) == 7);
    CHECK(!s.IsEmpty());
    CHECK(s.IsDirty());
    s.Set(1, 2, 3, kAir);
    CHECK(s.IsEmpty());
    if (g_fail == 0)
        printf("OK BlockStoreTest\n");
    return g_fail;
}
