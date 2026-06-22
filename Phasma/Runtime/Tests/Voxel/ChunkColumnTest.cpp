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
    ChunkColumn col({2, -3});
    CHECK((col.Coord() == ColumnCoord{2, -3})); // extra parens: braced-init comma vs macro
    col.SetLocal(3, 40, 9, 5);
    CHECK(col.GetLocal(3, 40, 9) == 5);
    CHECK(SectionIndex(40) == 2 && LocalY(40) == 8);
    CHECK(col.Section(2).Get(3, 8, 9) == 5);         // same block via section index
    CHECK(col.GetLocal(3, kWorldHeight, 9) == kAir); // out of range -> air
    col.SetLocal(3, kWorldHeight + 10, 9, 7);        // no crash, no-op
    CHECK(col.GetLocal(3, kWorldHeight + 10, 9) == kAir);
    CHECK(col.SectionInBounds(0) && col.SectionInBounds(kWorldHeight - 1));
    CHECK(!col.SectionInBounds(-1) && !col.SectionInBounds(kWorldHeight));
    if (g_fail == 0)
        printf("OK ChunkColumnTest\n");
    return g_fail;
}
