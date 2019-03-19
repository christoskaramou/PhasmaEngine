#ifndef COMMON_H_
#define COMMON_H_

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

#endif