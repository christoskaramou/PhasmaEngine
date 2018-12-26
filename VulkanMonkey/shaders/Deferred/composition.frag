#version 450

const float PI = 3.141592653589793f;

struct Material{
	vec3 albedo;
	float roughness;
	vec3 F0;
	float metallic;
};

struct Light {
	vec4 color;
	vec4 position;
	vec4 attenuation; };

vec2 poissonDisk[8] = vec2[](
	vec2(0.493393f, 0.394269f),
	vec2(0.798547f, 0.885922f),
	vec2(0.247322f, 0.92645f),
	vec2(0.0514542f, 0.140782f),
	vec2(0.831843f, 0.00955229f),
	vec2(0.428632f, 0.0171514f),
	vec2(0.015656f, 0.749779f),
	vec2(0.758385f, 0.49617f));

layout(push_constant) uniform SS { vec4 effect; vec4 size; mat4 invViewProj; } screenSpace;
layout (constant_id = 0) const int NUM_LIGHTS = 1;
layout (set = 0, binding = 0) uniform sampler2D samplerDepth;
layout (set = 0, binding = 1) uniform sampler2D samplerNormal;
layout (set = 0, binding = 2) uniform sampler2D samplerAlbedo;
layout (set = 0, binding = 3) uniform sampler2D samplerSpecRoughMet;
layout (set = 0, binding = 4) uniform UBO { vec4 camPos; Light lights[NUM_LIGHTS+1]; } ubo;
layout (set = 0, binding = 5) uniform sampler2D ssaoBlurSampler;
layout (set = 0, binding = 6) uniform sampler2D ssrSampler;
layout (set = 1, binding = 1) uniform sampler2DShadow shadowMapSampler;


layout (location = 0) in vec2 inUV;
layout (location = 1) in float castShadows;
layout (location = 2) in mat4 shadow_coords;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;
  
float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);
vec3 BRDFShadows(Material material, vec3 fragPos, vec3 normal, int light);
vec3 BRDF(Material material, vec3 fragPos, vec3 normal, int light);
vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular);
float shadowMultiSample(vec3 coords);
vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular);
vec3 getWorldPosFromUV(vec2 UV);

void main() 
{
	vec3 fragPos = getWorldPosFromUV(inUV);
	vec3 normal = texture(samplerNormal, inUV).xyz;
	vec4 albedo = texture(samplerAlbedo, inUV);
	float oclusion = texture(ssaoBlurSampler, inUV).x;
	vec3 specRoughMet = texture(samplerSpecRoughMet, inUV).xyz;

	Material material;
	material.albedo = albedo.xyz;
	material.roughness = 1.0 - specRoughMet.y; // for pbr .obj models only, else the "1.0 - " should not be there
	material.metallic = 1.0 - specRoughMet.z; // for pbr .obj models only, else the "1.0 - " should not be there
	material.F0 = mix(vec3(0.04f), material.albedo, material.metallic);

	// Ambient
	vec3 fragColor = 0.3 * albedo.xyz;

	// SSAO
	if (screenSpace.effect.x > 0.5f)
		fragColor *= oclusion;

	fragColor += BRDFShadows(material, fragPos, normal, 0);
	//fragColor += calculateShadow(0, fragPos, normal, albedo.xyz, specRoughMet.x);

	for(int i = 1; i < NUM_LIGHTS+1; ++i){
		fragColor += BRDF(material, fragPos, normal, i);
		//fragColor += calculateColor(i, fragPos, normal, albedo.xyz, specRoughMet.x);
	}

	outColor = vec4(fragColor, albedo.a);

	// SSR
	if (screenSpace.effect.y > 0.5)
		outColor += vec4(texture(ssrSampler, inUV).xyz, 0.0);
	
	outComposition = outColor;
}

vec3 getWorldPosFromUV(vec2 UV)
{
	vec2 revertedUV = (UV - screenSpace.size.xy) / screenSpace.size.zw; // floating window correction
	vec4 ndcPos;
	ndcPos.xy = revertedUV * 2.0 - 1.0;
	ndcPos.z = texture(samplerDepth, UV).x; // sample from the gl_FragCoord.z image
	ndcPos.w = 1.0;
	
	vec4 clipPos = screenSpace.invViewProj * ndcPos;
	return (clipPos / clipPos.w).xyz;
}

vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{
	vec4 s_coords =  shadow_coords * vec4(fragPos, 1.0);
	s_coords.xy = s_coords.xy * 0.5 + 0.5;
	s_coords = s_coords / s_coords.w;
	float lit = 0.0;
	for (int i = 0; i < 4 * castShadows; i++)
		lit += 0.25 * (texture( shadowMapSampler, vec3( s_coords.xy + poissonDisk[i]*0.0008, s_coords.z+0.0001 )));

	// Light to fragment
	vec3 L = ubo.lights[mainLight].position.xyz - fragPos;

	// Viewer to fragment
	vec3 V = ubo.camPos.xyz - fragPos;

	// Diffuse part
	vec3 diff = ubo.lights[mainLight].color.xyz * albedo.xyz * ubo.lights[mainLight].color.a;

	// Specular part
	L = normalize(L);
	vec3 N = normalize(normal);
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[mainLight].color.xyz * specular * pow(RdotV, 32.0);

	lit *= dot(N, L);
	return lit * (diff + spec);
}

vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{
	// Light to fragment
	vec3 L =  fragPos - ubo.lights[light].position.xyz;

	// Distance from light to fragment
	float dist = length(L);

	// Viewer to fragment
	vec3 V = fragPos - ubo.camPos.xyz;

	// Diffuse part
	L = normalize(L);
	vec3 N = normalize(normal);
	float LdotN = max(0.0, dot(L, N));
	vec3 diff = ubo.lights[light].color.xyz * albedo.xyz * LdotN;

	// Specular part
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[light].color.xyz * specular * pow(RdotV, 32.0);
	
	// Attenuation
	float atten = 1.0 / pow(1.0 + dist, 2);

	return atten * (diff + spec);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

vec3 BRDF(Material material, vec3 fragPos, vec3 normal, int light)
{
	// calculate per-light radiance
	vec3 N = normalize(normal);
    vec3 V = normalize(ubo.camPos.xyz - fragPos);
	vec3 L = normalize(ubo.lights[light].position.xyz - fragPos);
	vec3 H = normalize(V + L);
	float distance    = length(L);
	float attenuation = 1.0 / (1.0 + 3.6 * distance + 7.06 * (distance * distance));
	vec3 radiance     = ubo.lights[light].color.xyz * attenuation;
	
	// cook-torrance brdf
	float NDF = DistributionGGX(N, H, material.roughness);        
	float G   = GeometrySmith(N, V, L, material.roughness);      
	vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), material.F0);       
	
	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - material.metallic;	  
	
	vec3 numerator    = NDF * G * F;
	float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
	vec3 specular     = numerator / max(denominator, 0.001);  
	    
	// outgoing radiance
	float NdotL = max(dot(N, L), 0.0);                
	return (kD * material.albedo / PI + specular) * radiance * NdotL; 
}

vec3 BRDFShadows(Material material, vec3 fragPos, vec3 normal, int light)
{
	vec4 s_coords =  shadow_coords * vec4(fragPos, 1.0);
	s_coords.xy = s_coords.xy * 0.5 + 0.5;
	s_coords = s_coords / s_coords.w;
	float lit = 0.0;
	for (int i = 0; i < 4 * castShadows; i++)
		lit += 0.25 * (texture( shadowMapSampler, vec3( s_coords.xy + poissonDisk[i]*0.0008, s_coords.z+0.0001 )));

	// calculate per-light radiance
	vec3 N = normalize(normal);
    vec3 V = normalize(ubo.camPos.xyz - fragPos);
	vec3 L = normalize(ubo.lights[light].position.xyz - fragPos);
	vec3 H = normalize(V + L);
	vec3 radiance     = ubo.lights[light].color.xyz;        
	
	// cook-torrance brdf
	float NDF = DistributionGGX(N, H, material.roughness);        
	float G   = GeometrySmith(N, V, L, material.roughness);      
	vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), material.F0);       
	
	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - material.metallic;	  
	
	vec3 numerator    = NDF * G * F;
	float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
	vec3 specular     = numerator / max(denominator, 0.001);  
	    
	// outgoing radiance
	float NdotL = max(dot(N, L), 0.0);                
	return (kD * material.albedo / PI + specular) * radiance * NdotL * lit * 1.5; //intensity; 
}