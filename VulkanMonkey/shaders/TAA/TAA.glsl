#ifndef TAA_H_
#define TAA_H_

#extension GL_GOOGLE_include_directive : require
#include "../Common/common.glsl"
#include "../Common/tonemapping.glsl"

vec3 clip_aabb(vec3 aabb_min, vec3 aabb_max, vec3 p, vec3 q)
{
	vec3 r = q - p;
	vec3 rmax = aabb_max - p;
	vec3 rmin = aabb_min - p;
	
	if (r.x > rmax.x + FLT_EPS)
		r *= (rmax.x / r.x);
	if (r.y > rmax.y + FLT_EPS)
		r *= (rmax.y / r.y);
	if (r.z > rmax.z + FLT_EPS)
		r *= (rmax.z / r.z);
	
	if (r.x < rmin.x - FLT_EPS)
		r *= (rmin.x / r.x);
	if (r.y < rmin.y - FLT_EPS)
		r *= (rmin.y / r.y);
	if (r.z < rmin.z - FLT_EPS)
		r *= (rmin.z / r.z);
	
	return p + r;
}

vec4 ResolveTAA(vec2 texCoord, sampler2D tex_history, sampler2D tex_current, sampler2D tex_velocity, sampler2D tex_depth, float g_blendMin, float g_blendMax)
{
	// Reproject
	vec2 velocity = dilate_Depth3X3(tex_velocity, tex_depth, texCoord).xy;
	vec2 texCoord_history = texCoord - velocity;
	
	// Get current and history colors
	vec3 color_current 	= texture(tex_current, texCoord).xyz;
	vec3 color_history 	= texture(tex_history, texCoord_history).xyz;

	ivec2 texDim = textureSize(tex_depth, 0);
	vec2 g_resolution = vec2(float(texDim.x), float(texDim.y));
	vec2 g_texelSize = 1.0 / g_resolution;

	vec2 du = vec2(g_texelSize.x, 0.0f);
	vec2 dv = vec2(0.0f, g_texelSize.y);

	vec3 ctl = texture(tex_current, texCoord - dv - du).xyz;
	vec3 ctc = texture(tex_current, texCoord - dv).xyz;
	vec3 ctr = texture(tex_current, texCoord - dv + du).xyz;
	vec3 cml = texture(tex_current, texCoord - du).xyz;
	vec3 cmc = texture(tex_current, texCoord).xyz;
	vec3 cmr = texture(tex_current, texCoord + du).xyz;
	vec3 cbl = texture(tex_current, texCoord + dv - du).xyz;
	vec3 cbc = texture(tex_current, texCoord + dv).xyz;
	vec3 cbr = texture(tex_current, texCoord + dv + du).xyz;
	
	vec3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
	vec3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
	vec3 color_avg = vec3(ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0;
	
	color_history = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

	float factor_subpixel = saturate(length(velocity * g_resolution));

	float blendfactor = is_saturated(texCoord_history) ? lerp(g_blendMin, g_blendMax, factor_subpixel) : 1.0;
	
	color_history = Reinhard(color_history);
	color_current = Reinhard(color_current);

	vec3 resolved = lerp(color_history, color_current, blendfactor);
	
	return vec4(ReinhardInverse(resolved), 1.0f);
}

#endif