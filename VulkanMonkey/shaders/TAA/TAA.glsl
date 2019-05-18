#ifndef TAA_H_
#define TAA_H_

#extension GL_GOOGLE_include_directive : require
#include "../Common/common.glsl"

const float g_blendMin = 0.1f;
const float g_blendMax = 3.0f;

// TODO: This can be improved further by weighting the blend factor from unbiased luminance diff

// Resolving works better with tonemapped input
vec4 Reinhard(vec4 color)
{
	return color / (1 + color);
}

// Inverse tonemapping before returning
vec4 ReinhardInverse(vec4 color)
{
	return -color / (color - 1);
}

vec4 ResolveTAA(vec2 texCoord, sampler2D tex_history, sampler2D tex_current, sampler2D tex_velocity, sampler2D tex_depth)
{
	// Reproject
	vec2 velocity = dilate_Depth3X3(tex_velocity, tex_depth, texCoord).xy;
	vec2 texCoord_history = texCoord - velocity;
	
	// Get current and history colors
	vec4 color_current 	= Reinhard(texture(tex_current, texCoord));
	vec4 color_history 	= Reinhard(texture(tex_history, texCoord_history));

	ivec2 texDim = textureSize(tex_depth, 0);
	vec2 g_texelSize = 1.0 / vec2(float(texDim.x), float(texDim.y));
	//= Clamp out too different history colors (for non-existing and lighting change cases) =========================
	vec2 du = vec2(g_texelSize.x, 0.0f);
	vec2 dv = vec2(0.0f, g_texelSize.y);

	vec4 ctl = texture(tex_current, texCoord - dv - du);
	vec4 ctc = texture(tex_current, texCoord - dv);
	vec4 ctr = texture(tex_current, texCoord - dv + du);
	vec4 cml = texture(tex_current, texCoord - du);
	vec4 cmc = texture(tex_current, texCoord);
	vec4 cmr = texture(tex_current, texCoord + du);
	vec4 cbl = texture(tex_current, texCoord + dv - du);
	vec4 cbc = texture(tex_current, texCoord + dv);
	vec4 cbr = texture(tex_current, texCoord + dv + du);
	
	vec4 color_min = Reinhard(min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr)))))))));
	vec4 color_max = Reinhard(max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr)))))))));
	
	color_history = clamp(color_history, color_min, color_max);
	//===============================================================================================================
	
	//= Compute blend factor ==========================================================================
	float factor_subpixel = abs(sin(fract(length(velocity)) * PI)); // Decrease if motion is sub-pixel
	float blendfactor = mix(g_blendMin, g_blendMax, clamp(factor_subpixel, 0.0, 1.0));
	// Don't resolve when re-projected texcoord is out of screen
	blendfactor = is_saturated(texCoord_history) ? blendfactor : g_blendMax;
	//=================================================================================================
	
	vec4 resolved_tonemapped 	= mix(color_history, color_current, blendfactor);
	vec4 resolved 			= ReinhardInverse(resolved_tonemapped);
	
	return resolved;
}

#endif