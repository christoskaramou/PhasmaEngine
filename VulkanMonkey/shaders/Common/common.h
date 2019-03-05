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

#endif