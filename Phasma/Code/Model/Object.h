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

#include "Core/Base.h"
#include "Core/Math.h"

namespace pe
{
	class Image;
	class Buffer;

	class Object
	{
	public:
		Object();
		
		virtual ~Object() = default;
		
		bool render = true, cull = false;
		DescriptorSetHandle descriptorSet;
		Image* texture;
		std::vector<float> vertices {};
		Buffer* vertexBuffer;
		Buffer* indexBuffer;
		Buffer* uniformBuffer;
		
		virtual void createVertexBuffer();
		
		virtual void createUniformBuffer(size_t size);
		
		virtual void loadTexture(const std::string& path);
		
		virtual void createDescriptorSet(DescriptorSetLayoutHandle descriptorSetLayout);
		
		virtual void destroy();
	};
}