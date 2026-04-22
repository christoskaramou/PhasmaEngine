// BitonicSort compute shader - translated from webgpu-samples bitonicCompute.ts.
//
// Workgroup size is 64 threads; each thread sorts 2 elements, so local_data holds
// 128 uint32s per workgroup. For 1024 total elements we dispatch 1024 / (64*2) = 8
// workgroups per pass. Host drives the flip/disperse state machine.

#define WORKGROUP_SIZE 64
#define LOCAL_CAPACITY (WORKGROUP_SIZE * 2)

// algo enum values (mirrored on the host):
//   0 = NONE
//   1 = FLIP_LOCAL
//   2 = DISPERSE_LOCAL
//   3 = FLIP_GLOBAL
//   4 = DISPERSE_GLOBAL
#define ALGO_NONE            0
#define ALGO_LOCAL_FLIP      1
#define ALGO_LOCAL_DISPERSE  2
#define ALGO_GLOBAL_FLIP     3
#define ALGO_GLOBAL_DISPERSE 4

struct Uniforms
{
    uint blockHeight;
    uint algo;
};

[[vk::binding(0, 0)]] StructuredBuffer<uint>   input_data  : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output_data : register(u1, space0);
[[vk::binding(2, 0)]] cbuffer UBO                           : register(b2, space0)
{
    Uniforms uniforms;
}
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> counter : register(u3, space0);

groupshared uint local_data[LOCAL_CAPACITY];

void local_compare_and_swap(uint idx_before, uint idx_after)
{
    if (local_data[idx_after] < local_data[idx_before])
    {
        uint unused;
        InterlockedAdd(counter[0], 1u, unused);
        uint temp              = local_data[idx_before];
        local_data[idx_before] = local_data[idx_after];
        local_data[idx_after]  = temp;
    }
}

uint2 get_flip_indices(uint invoke_id, uint block_height)
{
    uint block_offset = ((2u * invoke_id) / block_height) * block_height;
    uint half_height  = block_height / 2u;
    uint2 idx         = uint2(invoke_id % half_height,
                              block_height - (invoke_id % half_height) - 1u);
    idx.x += block_offset;
    idx.y += block_offset;
    return idx;
}

uint2 get_disperse_indices(uint invoke_id, uint block_height)
{
    uint block_offset = ((2u * invoke_id) / block_height) * block_height;
    uint half_height  = block_height / 2u;
    uint2 idx         = uint2(invoke_id % half_height,
                              (invoke_id % half_height) + half_height);
    idx.x += block_offset;
    idx.y += block_offset;
    return idx;
}

void global_compare_and_swap(uint idx_before, uint idx_after)
{
    if (input_data[idx_after] < input_data[idx_before])
    {
        output_data[idx_before] = input_data[idx_after];
        output_data[idx_after]  = input_data[idx_before];
    }
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
void CSMain(uint3 global_id    : SV_DispatchThreadID,
            uint3 local_id     : SV_GroupThreadID,
            uint3 workgroup_id : SV_GroupID)
{
    uint offset = LOCAL_CAPACITY * workgroup_id.x;

    // Populate groupshared for local algorithms.
    if (uniforms.algo <= ALGO_LOCAL_DISPERSE)
    {
        local_data[local_id.x * 2u]      = input_data[offset + local_id.x * 2u];
        local_data[local_id.x * 2u + 1u] = input_data[offset + local_id.x * 2u + 1u];
    }

    GroupMemoryBarrierWithGroupSync();

    switch (uniforms.algo)
    {
        case ALGO_LOCAL_FLIP:
        {
            uint2 idx = get_flip_indices(local_id.x, uniforms.blockHeight);
            local_compare_and_swap(idx.x, idx.y);
            break;
        }
        case ALGO_LOCAL_DISPERSE:
        {
            uint2 idx = get_disperse_indices(local_id.x, uniforms.blockHeight);
            local_compare_and_swap(idx.x, idx.y);
            break;
        }
        case ALGO_GLOBAL_FLIP:
        {
            uint2 idx = get_flip_indices(global_id.x, uniforms.blockHeight);
            global_compare_and_swap(idx.x, idx.y);
            break;
        }
        case ALGO_GLOBAL_DISPERSE:
        {
            uint2 idx = get_disperse_indices(global_id.x, uniforms.blockHeight);
            global_compare_and_swap(idx.x, idx.y);
            break;
        }
        default:
            break;
    }

    GroupMemoryBarrierWithGroupSync();

    // Write groupshared back out for local algorithms.
    if (uniforms.algo <= ALGO_LOCAL_DISPERSE)
    {
        output_data[offset + local_id.x * 2u]      = local_data[local_id.x * 2u];
        output_data[offset + local_id.x * 2u + 1u] = local_data[local_id.x * 2u + 1u];
    }
}
