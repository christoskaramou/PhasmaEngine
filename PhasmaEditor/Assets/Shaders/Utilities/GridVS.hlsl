struct VS_OUTPUT_GRID
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 color : TEXCOORD1;
};

static const int MAX_DATA_SIZE = 2048;
[[vk::binding(0)]] tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

static const int GRID_HALF_SIZE = 500; // Number of lines in each direction
static const int GRID_SPACING = 1;     // Units between lines

VS_OUTPUT_GRID mainVS(uint VertexIndex : SV_VertexID)
{
    VS_OUTPUT_GRID output;
    
    uint linesPerAxis = 2 * GRID_HALF_SIZE + 1;
    uint verticesPerAxis = linesPerAxis * 2;
    
    float4x4 invView = data[2];
    float3 cameraPos = float3(invView[3][0], invView[3][1], invView[3][2]);
    
    // Snap camera to grid spacing to prevent swimming
    float snappedX = floor(cameraPos.x / (float)GRID_SPACING) * (float)GRID_SPACING;
    float snappedZ = floor(cameraPos.z / (float)GRID_SPACING) * (float)GRID_SPACING;
    
    float3 pos = float3(0, 0, 0);
    float3 color = float3(0.5, 0.5, 0.5); // Grey default
    
    // Determine Axis
    if (VertexIndex < verticesPerAxis) 
    {
        uint lineIndex = VertexIndex / 2;
        int relativeIndex = (int)lineIndex - GRID_HALF_SIZE;
        float xOffset = (float)relativeIndex * (float)GRID_SPACING;
        
        pos.x = snappedX + xOffset;
        pos.y = 0.0;
        
        // Line Extents
        float zStart = snappedZ - (float)GRID_HALF_SIZE * (float)GRID_SPACING;
        float zEnd   = snappedZ + (float)GRID_HALF_SIZE * (float)GRID_SPACING;
        
        pos.z = (VertexIndex % 2 == 0) ? zStart : zEnd;
        
        if (abs(pos.x) < 0.01)
            color = float3(0.0, 0.0, 1.0); 
    }
    else
    {
        uint adjustedIndex = VertexIndex - verticesPerAxis;
        uint lineIndex = adjustedIndex / 2;
        int relativeIndex = (int)lineIndex - GRID_HALF_SIZE;
        float zOffset = (float)relativeIndex * (float)GRID_SPACING;
        
        pos.z = snappedZ + zOffset;
        pos.y = 0.0;
        
        float xStart = snappedX - (float)GRID_HALF_SIZE * (float)GRID_SPACING;
        float xEnd   = snappedX + (float)GRID_HALF_SIZE * (float)GRID_SPACING;
        
        pos.x = (adjustedIndex % 2 == 0) ? xStart : xEnd;
        
        if (abs(pos.z) < 0.01)
            color = float3(1.0, 0.0, 0.0);
    }
    
    output.worldPos = pos;
    output.color = color;
    
    float4x4 viewProj = data[0];
    output.position = mul(float4(pos, 1.0), viewProj);
    
    return output;
}
