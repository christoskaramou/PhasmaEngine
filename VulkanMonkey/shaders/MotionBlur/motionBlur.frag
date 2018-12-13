#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout (set = 0, binding = 1) uniform sampler2D depthSampler;
layout (set = 0, binding = 2) uniform UniformBufferObject { mat4 projection; mat4 view; mat4 previousView; mat4 invViewProj; } ubo;
layout(push_constant) uniform Constants { vec4 fps; vec4 offset; } pushConst;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int samples = 8;

vec3 getWorldPosFromUV(vec2 UV)
{
	vec2 revertedUV = (UV - pushConst.offset.xy) / pushConst.offset.zw; // floating window correction
	vec4 ndcPos;
	ndcPos.xy = revertedUV * 2.0 - 1.0;
	ndcPos.z = texture(depthSampler, UV).x; // sample from the gl_FragCoord.z image
	ndcPos.w = 1.0;
	
	vec4 clipPos = ubo.invViewProj * ndcPos;
	return (clipPos / clipPos.w).xyz;
}


void main() 
{
	vec2 UV = inUV;
	vec3 worldPos = getWorldPosFromUV(UV);
	vec3 currentPos = (ubo.view * vec4(worldPos, 1.0)).xyz;
	vec3 previousPos = (ubo.previousView * vec4(worldPos, 1.0)).xyz;
	vec3 viewVelocity = currentPos - previousPos;

	vec2 velocity = (ubo.projection * vec4(viewVelocity, 1.0)).xy;
	velocity *= 0.5; // -0.5 to 0.5;
	velocity *= pushConst.offset.zw; // floating window velocity aspect correction
	velocity /= 1.0 + (currentPos.z + previousPos.z) * 0.5; // "1 + depth" division, so far pixels wont blur so much
	velocity *= pushConst.fps.x; // fix for low and high fps that giving different velocities
	velocity *= 0.001; // scale the effect

	vec4 color = vec4(0.0);
	for (int i = 0; i < samples; i++, UV += velocity)
	{
		color += texture(compositionSampler, UV);
	}
	outColor = color / samples;
}