#ifndef TAA_H_
#define TAA_H_

#extension GL_GOOGLE_include_directive : require
#include "../Common/common.glsl"

const float g_blendMin = 0.03f;
const float g_blendMax = 0.99f;

// TODO: This can be improved further by weighting the blend factor from unbiased luminance diff

// Resolving works better with tonemapped input
vec4 Reinhard(vec4 color)
{
	return vec4(color.xyz / (1 + color.xyz), color.w);
}

// Inverse tonemapping before returning
vec4 ReinhardInverse(vec4 color)
{
	return vec4(-color.xyz / (color.xyz - 1), color.w);
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

	float factor_subpixel = clamp(length(velocity) / length(g_texelSize), 0.0, 1.0);

	vec4 dmax = color_max - color_history;
	vec4 dmin = color_history - color_min;
	float factor_color = clamp(min((dmax.r + dmax.g + dmax.b) * 0.3333, (dmin.r + dmin.g + dmin.b) * 0.3333) * 10.0, 0.0, 1.0);

	vec3 cLuma = vec3(0.2126, 0.7152, 0.0722);
	vec3 color_luma_diff = abs(color_history - color_current).xyz * cLuma;
	float factor_contrast = 1.0 - color_luma_diff.r - color_luma_diff.g - color_luma_diff.b;
	
	float blendfactor = mix(g_blendMin, g_blendMax, factor_color * factor_subpixel * factor_contrast);

	blendfactor = is_saturated(texCoord_history) ? g_blendMax : blendfactor;
	
	vec4 resolved_tonemapped = mix(color_history, color_current, blendfactor);
	vec4 resolved = ReinhardInverse(resolved_tonemapped);
	
	return resolved;
}

#endif