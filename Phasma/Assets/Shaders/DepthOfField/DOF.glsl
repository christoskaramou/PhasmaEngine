/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef DOF_H_
#define DOF_H_

// ----- Bokeh Depth of Field ----
const float GOLDEN_ANGLE = 2.39996323;
const float RAD_SCALE = 0.5;
const float FAR_PLANE = 500.0;

// Sample a wide pixel average depth
float depth_average_wide(sampler2D sampler_depth, vec2 texCoord, float pixel_range)
{
    ivec2 texDim = textureSize(sampler_depth, 0);
    float dx = pixel_range * float(texDim.x);
    float dy = pixel_range * float(texDim.y);

    float tl = texture(sampler_depth, texCoord + vec2(-dx, -dy)).r;
    float tr = texture(sampler_depth, texCoord + vec2(+dx, -dy)).r;
    float bl = texture(sampler_depth, texCoord + vec2(-dx, +dy)).r;
    float br = texture(sampler_depth, texCoord + vec2(+dx, +dy)).r;
    float ce = texture(sampler_depth, texCoord).r;

    return (tl + tr + bl + br + ce) * 0.2;
}

float getBlurSize(float depth, float center_depth, float focus_scale, float blur_range)
{
    float coc = abs(center_depth - depth) * focus_scale * FAR_PLANE;
    return clamp(coc, 0.0, blur_range);
}

vec3 depthOfField(sampler2D sampler_color, sampler2D sampler_depth, vec2 tex_coord, float focus_scale, float blur_range)
{
    float center_depth = depth_average_wide(sampler_depth, vec2(0.5, 0.5), 20.0);
    vec3 color = texture(sampler_color, tex_coord).rgb;
    float tot = 1.0;

    ivec2 tex_dim = textureSize(sampler_color, 0);
    vec2 pixel_size = 1.0 / vec2(float(tex_dim.x), float(tex_dim.y));

    float radius = RAD_SCALE;
    for (float ang = 0.0; radius < blur_range; ang += GOLDEN_ANGLE)
    {
        vec2 tc = tex_coord + vec2(cos(ang), sin(ang)) * pixel_size * radius;

        vec3 sample_color = texture(sampler_color, tc).rgb;
        float sample_depth = texture(sampler_depth, tc).r;
        float sample_size = getBlurSize(sample_depth, center_depth, focus_scale, blur_range);

        float m = smoothstep(radius-0.5, radius+0.5, sample_size);
        color += mix(color/tot, sample_color, m);
        tot += 1.0;
        radius += RAD_SCALE/radius;
    }
    return color /= tot;
}

#endif
