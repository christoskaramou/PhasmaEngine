#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 0, binding = 0) uniform sampler2D samplerDepth;
layout (set = 0, binding = 1) uniform sampler2D samplerNormal;
layout (set = 0, binding = 2) uniform sampler2D samplerNoise;
layout (set = 0, binding = 3) uniform UniformBufferObject { vec4 samples[64]; } kernel;
layout (set = 0, binding = 4) uniform UniformBufferPVM { mat4 projection; mat4 view; mat4 invProjection; } pvm;
layout(push_constant) uniform Position { vec4 offset; } pos;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int KERNEL_SIZE = 16;
const float MAX_DISTANCE = 25.0;
const float RADIUS = 0.5;
const float bias = 0.001;

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = getPosFromUV(inUV, texture(samplerDepth, inUV).x, pvm.invProjection, pos.offset);
	vec4 normal = pvm.view * texture(samplerNormal, inUV);
	
	float radius = RADIUS / fragPos.z;
	float max_distance_inv = 1.f / MAX_DISTANCE;
	float attenuation_angle_threshold = 0.1;

	// Get a random vector using a noise lookup
	ivec2 texDim = textureSize(samplerDepth, 0); 
	ivec2 noiseDim = textureSize(samplerNoise, 0);
	const vec2 noiseUV = vec2(float(texDim.x)/float(noiseDim.x), float(texDim.y)/(noiseDim.y)) * inUV;  
	vec3 randomVec = texture(samplerNoise, noiseUV).xyz * 2.0 - 1.0;
	
	// Create TBN matrix
	vec3 tangent = normalize(randomVec - normal.xyz * dot(randomVec, normal.xyz));
	vec3 bitangent = cross(normal.xyz, tangent);
	mat3 TBN = mat3(tangent, bitangent, normal.xyz);

	const float fudge_factor_l0 = 2.0;
	const float fudge_factor_l1 = 10.0;
	const float sh2_weight_l0 = fudge_factor_l0 * 0.28209;
	const vec3 sh2_weight_l1 = vec3(fudge_factor_l1 * 0.48860);
	const vec4 sh2_weight = vec4(sh2_weight_l1, sh2_weight_l0) / KERNEL_SIZE;
	
	// Calculate occlusion value
	vec4 occlusion = vec4(0.0);
	for(int i = 0; i < KERNEL_SIZE; i++)
	{
		vec3 direction = TBN * kernel.samples[i].xyz * radius;
		vec4 newViewPos = vec4(fragPos + direction, 1.0);
		vec4 samplePosition = pvm.projection * newViewPos;
		samplePosition.xy /= samplePosition.w;
		samplePosition.xy = samplePosition.xy * 0.5f + 0.5f;
		samplePosition.xy *= pos.offset.zw; // floating window size correction
		samplePosition.xy += pos.offset.xy; // floating window position correction

		vec3 sampledPos = getPosFromUV(samplePosition.xy, texture(samplerDepth, samplePosition.xy).x, pvm.invProjection, pos.offset);
		vec3 f2s = sampledPos - fragPos;
		float dist = length(f2s);
		vec3 f2sNorm = f2s / dist;
		float attenuation = 1 - clamp(dist * max_distance_inv, 0.0, 1.0);
		float dp = dot(normal.xyz, f2sNorm);

        attenuation = attenuation * attenuation * step(attenuation_angle_threshold, dp);

		occlusion += attenuation * sh2_weight * vec4(f2sNorm, 1.0);
	}

	outColor = normalize(inverse(pvm.view) * vec4(occlusion.xyz, 0.0));
	outColor.w = 1.0;
}