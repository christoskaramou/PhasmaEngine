#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Light {
	vec4 color;
	vec4 position;
	vec4 attenuation;
	vec4 camPos; };

vec2 poissonDisk[8] = vec2[](
	vec2(0.493393f, 0.394269f),
	vec2(0.798547f, 0.885922f),
	vec2(0.247322f, 0.92645f),
	vec2(0.0514542f, 0.140782f),
	vec2(0.831843f, 0.00955229f),
	vec2(0.428632f, 0.0171514f),
	vec2(0.015656f, 0.749779f),
	vec2(0.758385f, 0.49617f));
	
layout (constant_id = 0) const int NUM_LIGHTS = 1;
layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput samplerPosition;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput samplerNormal;
layout (input_attachment_index = 2, set = 0, binding = 2) uniform subpassInput samplerAlbedo;
layout (input_attachment_index = 3, set = 0, binding = 3) uniform subpassInput samplerSpecular;
layout (set = 0, binding = 4) uniform UBO { Light lights[NUM_LIGHTS]; } ubo;
layout (set = 1, binding = 1) uniform sampler2DShadow shadowMapSampler;


layout (location = 0) in vec2 inUV;
layout (location = 1) in float castShadows;
layout (location = 2) in mat4 shadow_coords;

layout (location = 0) out vec4 outColor;

vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular);
vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular);

void main() 
{
	// Read G-Buffer values from previous sub pass
	vec4 fragPos = subpassLoad(samplerPosition);
	vec3 normal = subpassLoad(samplerNormal).rgb;
	vec4 albedo = subpassLoad(samplerAlbedo);
	float specular = subpassLoad(samplerSpecular).r;

	// Ambient
	vec3 fragColor = 0.05 * albedo.rgb;

	fragColor += calculateShadow(0, fragPos.xyz, normal, albedo.rgb, specular);

	for(int i = 1; i < NUM_LIGHTS; ++i)
		fragColor += calculateColor(i, fragPos.xyz, normal, albedo.rgb, specular);

	outColor = vec4(fragColor, albedo.a);
}

vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{

	vec4 s_coords =  shadow_coords * vec4(fragPos.xyz, 1.0);
	s_coords = s_coords / s_coords.w;
	float shadow = 0.0;
	for (int i = 0; i < 8 * castShadows; i++)
		shadow += 0.125 * texture( shadowMapSampler, vec3( s_coords.xy + poissonDisk[i]*0.0008, s_coords.z-0.0001 ));

	// Light to fragment
	vec3 L = fragPos - ubo.lights[mainLight].position.xyz;

	// Viewer to fragment
	vec3 V = fragPos - ubo.lights[mainLight].camPos.xyz;

	// Diffuse part
	vec3 diff = ubo.lights[mainLight].color.rgb * albedo.rgb * ubo.lights[mainLight].color.a;

	// Specular part
	L = normalize(L);
	vec3 N = normalize(normal);
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[mainLight].color.rgb * specular * pow(RdotV, 32.0);

	return shadow * (diff + spec);
}

vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{
	// Light to fragment
	vec3 L =  fragPos - ubo.lights[light].position.xyz;

	// Distance from light to fragment
	float dist = length(L);
	if (dist > ubo.lights[light].attenuation.x*5.0)
		return vec3(0.0);

	// Viewer to fragment
	vec3 V = fragPos - ubo.lights[light].camPos.xyz;

	// Diffuse part
	L = normalize(L);
	vec3 N = normalize(normal);
	float LdotN = max(0.0, dot(L, N));
	vec3 diff = ubo.lights[light].color.rgb * albedo.rgb * LdotN;

	// Specular part
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[light].color.rgb * specular * pow(RdotV, 32.0);
	
	// Attenuation
	float atten = 1.0 / (1.0 + ubo.lights[light].attenuation.x * pow(dist, 2));

	return atten * (diff + spec);
}