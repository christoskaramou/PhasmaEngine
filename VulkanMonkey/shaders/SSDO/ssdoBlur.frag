#version 450

layout (set = 0, binding = 0) uniform sampler2D samplerSSDO;
layout (set = 0, binding = 1) uniform sampler2D samplerNormal;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main() 
{
	float weights[9] =
	{
		0.013519569015984728,
		0.047662179108871855,
		0.11723004402070096,
		0.20116755999375591,
		0.240841295721373,
		0.20116755999375591,
		0.11723004402070096,
		0.047662179108871855,
		0.013519569015984728
	};

	float indices[9] = {-4, -3, -2, -1, 0, +1, +2, +3, +4};

	vec2 step = texture(samplerSSDO, inUV).xy / vec2(textureSize(samplerSSDO, 0));

	vec3 normal[9];
	normal[0] = texture(samplerNormal, inUV + indices[0]*step).xyz;
	normal[1] = texture(samplerNormal, inUV + indices[1]*step).xyz;
	normal[2] = texture(samplerNormal, inUV + indices[2]*step).xyz;
	normal[3] = texture(samplerNormal, inUV + indices[3]*step).xyz;
	normal[4] = texture(samplerNormal, inUV + indices[4]*step).xyz;
	normal[5] = texture(samplerNormal, inUV + indices[5]*step).xyz;
	normal[6] = texture(samplerNormal, inUV + indices[6]*step).xyz;
	normal[7] = texture(samplerNormal, inUV + indices[7]*step).xyz;
	normal[8] = texture(samplerNormal, inUV + indices[8]*step).xyz;

	float total_weight = 1.0;
	float discard_threshold = 0.85;

	int i;

	for( i=0; i<9; ++i )
	{
		if( dot(normal[i], normal[4]) < discard_threshold )
		{
			total_weight -= weights[i];
			weights[i] = 0;
		}
	}

	vec4 res = vec4(0.0);

	for( i=0; i<9; ++i )
	{
		res += texture(samplerSSDO, inUV + indices[i]*step) * weights[i];
	}

	res /= total_weight;

	outColor = res;
}