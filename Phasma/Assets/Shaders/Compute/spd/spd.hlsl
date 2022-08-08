// SPDSample
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//--------------------------------------------------------------------------------------
// Push Constants
//--------------------------------------------------------------------------------------
struct SpdConstants
{
    uint mips;
    uint numWorkGroups;
    uint2 workGroupOffset;
};

[[vk::push_constant]]
ConstantBuffer<SpdConstants> spdConstants;
//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
[[vk::binding(0)]] RWTexture2DArray<float4> imgDst[13] :register(u0); // don't access mip [6]
[[vk::binding(1)]] globallycoherent RWTexture2DArray<float4> imgDst6 :register(u1);

//--------------------------------------------------------------------------------------
// Buffer definitions - global atomic counter
//--------------------------------------------------------------------------------------
struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};
[[vk::binding(2)]] globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic;

// #define A_GPU
// #define A_HLSL

#include "ffx_a.h"

groupshared AU1 spdCounter;

// define fetch and store functions
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

AF4 SpdLoadSourceImage(ASU2 tex, AU1 slice)
{
    return imgDst[0][int3(tex,slice)];
}
AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    return imgDst6[int3(tex,slice)];
}
void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    if (index == 5)
    {
        imgDst6[int3(pix, slice)] = outValue;
        return;
    }
    imgDst[index + 1][int3(pix, slice)] = outValue;
}
void SpdIncreaseAtomicCounter(AU1 slice)
{
    InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    spdGlobalAtomic[0].counter[slice] = 0;
}
AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return AF4(
    spdIntermediateR[x][y], 
    spdIntermediateG[x][y], 
    spdIntermediateB[x][y], 
    spdIntermediateA[x][y]);
}
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return (v0+v1+v2+v3)*0.25;
}

#include "ffx_spd.h"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
[numthreads(256,1,1)]
void mainCS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    SpdDownsample(
        AU2(WorkGroupId.xy), 
        AU1(LocalThreadIndex),  
        AU1(spdConstants.mips),
        AU1(spdConstants.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(spdConstants.workGroupOffset));
}