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

#pragma once

#include "Core/Math.h"

namespace pe
{
    class VertexInputBindingDescription
    {
    public:
        uint32_t binding;
        uint32_t stride;
        VertexInputRate inputRate;
    };

    class VertexInputAttributeDescription
    {
    public:
        uint32_t location;
        uint32_t binding;
        Format format;
        uint32_t offset;
    };

    class Vertex
    {
    public:
        Vertex();

        Vertex(vec3 &pos, vec2 &uv, vec3 &norm, vec4 &color, ivec4 &bonesIDs, vec4 &weights);

        static std::vector <VertexInputBindingDescription> GetBindingDescriptionGeneral();

        static std::vector <VertexInputBindingDescription> GetBindingDescriptionGUI();

        static std::vector <VertexInputBindingDescription> GetBindingDescriptionSkyBox();

        static std::vector <VertexInputAttributeDescription> GetAttributeDescriptionGeneral();

        static std::vector <VertexInputAttributeDescription> GetAttributeDescriptionGUI();

        static std::vector <VertexInputAttributeDescription> GetAttributeDescriptionSkyBox();

        vec3 position;
        vec2 uv;
        vec3 normals;
        vec4 color;
        ivec4 bonesIDs;
        vec4 weights;
    };
}