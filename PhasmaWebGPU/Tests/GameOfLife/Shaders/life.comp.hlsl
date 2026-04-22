[[vk::binding(0, 0)]] StructuredBuffer<uint> size : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint> current : register(t1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> next : register(u2, space0);

uint getIndex(uint x, uint y)
{
    uint w = size[0];
    uint h = size[1];
    return (y % h) * w + (x % w);
}

uint getCell(uint x, uint y)
{
    return current[getIndex(x, y)];
}

uint countNeighbors(uint x, uint y)
{
    return getCell(x - 1, y - 1) + getCell(x, y - 1) + getCell(x + 1, y - 1) +
           getCell(x - 1, y)                         + getCell(x + 1, y) +
           getCell(x - 1, y + 1) + getCell(x, y + 1) + getCell(x + 1, y + 1);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 grid : SV_DispatchThreadID)
{
    uint x = grid.x;
    uint y = grid.y;
    uint n = countNeighbors(x, y);
    uint alive = getCell(x, y);
    uint result;
    if (alive == 1u)
        result = (n == 2u || n == 3u) ? 1u : 0u;
    else
        result = (n == 3u) ? 1u : 0u;
    next[getIndex(x, y)] = result;
}
