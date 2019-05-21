#ifndef COMMON_H_
#define COMMON_H_

#define PI 3.1415926535897932384626433832795

vec3 getPosFromUV(vec2 UV, float depth, mat4 mat, vec4 windowSize)
{
	vec2 revertedUV = (UV - windowSize.xy) / windowSize.zw; // floating window correction
	vec4 ndcPos;
	ndcPos.xy = revertedUV * 2.0 - 1.0;
	ndcPos.z = depth; // sample from the gl_FragCoord.z image
	ndcPos.w = 1.0;

	vec4 clipPos = mat * ndcPos;
	return (clipPos / clipPos.w).xyz;
}

// Find the normal for this fragment, pulling either from a predefined normal map
// or from the interpolated mesh normal and tangent attributes.
vec3 getNormal(vec3 inWorldPos, sampler2D normalMap, vec3 inNormal, vec2 inUV)
{
	// Perturb normal, see http://www.thetenthplanet.de/archives/1180
	vec3 tangentNormal = texture(normalMap, inUV).xyz * 2.0 - 1.0;

	vec3 q1 = dFdx(inWorldPos);
	vec3 q2 = dFdy(inWorldPos);
	vec2 st1 = dFdx(inUV);
	vec2 st2 = dFdy(inUV);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

// Return average velocity
vec3 dilate_Average(sampler2D samplerVelocity, vec2 texCoord)
{
	ivec2 texDim = textureSize(samplerVelocity, 0);
	float dx = 2.0f * float(texDim.x);
	float dy = 2.0f * float(texDim.y);
	
	vec3 tl = texture(samplerVelocity, texCoord + vec2(-dx, -dy)).xyz;
	vec3 tr = texture(samplerVelocity, texCoord + vec2(+dx, -dy)).xyz;
	vec3 bl = texture(samplerVelocity, texCoord + vec2(-dx, +dy)).xyz;
	vec3 br = texture(samplerVelocity, texCoord + vec2(+dx, +dy)).xyz;
	vec3 ce = texture(samplerVelocity, texCoord).xyz;
	
	return (tl + tr + bl + br + ce) * 0.2;
}

// Returns velocity with closest depth
vec3 dilate_Depth3X3(sampler2D samplerVelocity, sampler2D samplerDepth, vec2 texCoord)
{
	ivec2 texDim = textureSize(samplerDepth, 0);
	vec2 texelSize = 1.0 / vec2(float(texDim.x), float(texDim.y));
	float closestDepth = 0.0;
	vec2 closestTexCoord = texCoord;
    for(int y = -1; y <= 1; ++y)
    {
        for(int x = -1; x <= 1; ++x)
        {
			vec2 offset = vec2(x, y) * texelSize;
			float depth = texture(samplerDepth, texCoord + offset).r;
			if(depth > closestDepth) // using ">" because depth is reversed
			{
				closestTexCoord	= texCoord + offset;
			}
        }
	}

	return texture(samplerVelocity, closestTexCoord).xyz;
}

bool is_saturated(float x) 	{ return x == clamp(x, 0.0, 1.0); }
bool is_saturated(vec2 x) { return is_saturated(x.x) && is_saturated(x.y); }

float length2(vec2 x) { return dot(x, x); }
float length2(vec3 x) { return dot(x, x); }

#endif