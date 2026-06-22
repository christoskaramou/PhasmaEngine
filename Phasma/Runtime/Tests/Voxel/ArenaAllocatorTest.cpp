#include "Voxel/FreeListAllocator.h"
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
    FreeListAllocator a(1000);
    CHECK(a.Alloc(100) == 0);
    CHECK(a.Alloc(50) == 100);
    CHECK(a.Used() == 150);
    a.Free(0, 100);
    CHECK(a.Used() == 50);
    CHECK(a.Alloc(80) == 0); // reuses the freed front span (first-fit)
    CHECK(a.Alloc(100000) == FreeListAllocator::kInvalid);
    FreeListAllocator b(200);
    uint32_t x = b.Alloc(100); // 0
    uint32_t y = b.Alloc(100); // 100
    CHECK(x == 0 && y == 100);
    b.Free(0, 100);
    b.Free(100, 100); // coalesces with [0,100) -> one [0,200) span
    CHECK(b.Alloc(200) == 0);
    if (g_fail == 0)
        printf("OK ArenaAllocatorTest\n");
    return g_fail;
}
