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

#include "PhasmaPch.h"
#include "Mesh.h"
#include "../Renderer/Pipeline.h"
#include "../../Include/tinygltf/stb_image.h"
#include "../Renderer/RenderApi.h"
#include "../Core/Path.h"

namespace pe
{
	std::map<std::string, Image> Mesh::uniqueTextures {};
	
	Primitive::Primitive() : pbrMaterial({})
	{
		descriptorSet = make_ref(vk::DescriptorSet());
	}
	
	Primitive::~Primitive()
	{
	}
	
	Mesh::Mesh()
	{
		descriptorSet = make_ref(vk::DescriptorSet());
	}
	
	Mesh::~Mesh()
	{
	}
	
	void Mesh::createUniformBuffers()
	{
		uniformBuffer.CreateBuffer(
				sizeof(ubo), BufferUsage::UniformBuffer, MemoryProperty::HostVisible
		);
		uniformBuffer.Map();
		uniformBuffer.Zero();
		uniformBuffer.Flush();
		uniformBuffer.Unmap();
		
		for (auto& primitive : primitives)
		{
			
			mat4 factors;
			factors[0] = primitive.pbrMaterial.baseColorFactor != vec4(0.f) ? primitive.pbrMaterial.baseColorFactor :
			             vec4(1.f);
			factors[1] = vec4(primitive.pbrMaterial.emissiveFactor, 1.f);
			factors[2] = vec4(
					primitive.pbrMaterial.metallicFactor, primitive.pbrMaterial.roughnessFactor,
					primitive.pbrMaterial.alphaCutoff, 0.f
			);
			factors[3][0] = static_cast<float>(primitive.hasBones);
			
			const size_t size = sizeof(mat4);
			primitive.uniformBuffer.CreateBuffer(
					size, BufferUsage::UniformBuffer, MemoryProperty::HostVisible
			);
			primitive.uniformBuffer.Map();
			primitive.uniformBuffer.CopyData(&factors);
			primitive.uniformBuffer.Flush();
			primitive.uniformBuffer.Unmap();
		}
	}
	
	void Primitive::loadTexture(
			MaterialType type,
			const std::string& folderPath,
			const Microsoft::glTF::Image* image,
			const Microsoft::glTF::Document* document,
			const Microsoft::glTF::GLTFResourceReader* resourceReader
	)
	{
		std::string path = folderPath;
		if (image)
			path = folderPath + image->uri;
		
		// get the right texture
		Image* tex;
		switch (type)
		{
			case MaterialType::BaseColor:
				tex = &pbrMaterial.baseColorTexture;
				if (!image || image->uri.empty())
					path = Path::Assets + "Objects/black.png";
				break;
			case MaterialType::MetallicRoughness:
				tex = &pbrMaterial.metallicRoughnessTexture;
				if (!image || image->uri.empty())
					path = Path::Assets + "Objects/black.png";
				break;
			case MaterialType::Normal:
				tex = &pbrMaterial.normalTexture;
				if (!image || image->uri.empty())
					path = Path::Assets + "Objects/normal.png";
				break;
			case MaterialType::Occlusion:
				tex = &pbrMaterial.occlusionTexture;
				if (!image || image->uri.empty())
					path = Path::Assets + "Objects/white.png";
				break;
			case MaterialType::Emissive:
				tex = &pbrMaterial.emissiveTexture;
				if (!image || image->uri.empty())
					path = Path::Assets + "Objects/black.png";
				break;
			default:
				throw std::runtime_error("Load texture invalid type");
		}
		
		// Check if it is already loaded
		if (Mesh::uniqueTextures.find(path) != Mesh::uniqueTextures.end())
		{
			*tex = Mesh::uniqueTextures[path];
		}
		else
		{
			int texWidth, texHeight, texChannels;
			unsigned char* pixels;
			//stbi_set_flip_vertically_on_load(true);
			if (!image || image->bufferViewId.empty())
				pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
			else
			{
				const auto data = resourceReader->ReadBinaryData(*document, *image);
				pixels = stbi_load_from_memory(
						data.data(), static_cast<int>(data.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha
				);
			}
			
			if (!pixels)
				throw std::runtime_error("No pixel data loaded");
			
			const vk::DeviceSize imageSize = texWidth * texHeight * STBI_rgb_alpha;
			
			VulkanContext::Get()->graphicsQueue->waitIdle();
			VulkanContext::Get()->waitAndLockSubmits();
			
			Buffer staging;
			staging.CreateBuffer(
					imageSize, BufferUsage::TransferSrc, MemoryProperty::HostVisible
			);
			staging.Map();
			staging.CopyData(pixels);
			staging.Flush();
			staging.Unmap();
			
			stbi_image_free(pixels);
			
			tex->format = make_ref(vk::Format::eR8G8B8A8Unorm);
			tex->mipLevels =
					static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
			tex->createImage(
					texWidth, texHeight, vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
					vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal
			);
			tex->transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
			tex->copyBufferToImage(*staging.GetBufferVK());
			tex->generateMipMaps();
			tex->createImageView(vk::ImageAspectFlagBits::eColor);
			tex->maxLod = static_cast<float>(tex->mipLevels);
			tex->createSampler();
			
			staging.Destroy();
			
			VulkanContext::Get()->unlockSubmits();
			
			Mesh::uniqueTextures[path] = *tex;
		}
	}
	
	//void Mesh::calculateBoundingSphere()
	//{
	//	vec3 _max(-FLT_MAX);
	//	vec3 _min(FLT_MAX);
	//	for (auto& vertex : vertices) {
	//		_max = maximum(_max, vertex.position);
	//		_min = minimum(_min, vertex.position);
	//	}
	//	vec3 center = (_max + _min) * .5f;
	//	float sphereRadius = length(_max - center);
	//	boundingSphere = vec4(center, sphereRadius);
	//}
	
	void Mesh::destroy()
	{
		uniformBuffer.Destroy();
		if (Pipeline::getDescriptorSetLayoutMesh())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMesh());
			Pipeline::getDescriptorSetLayoutMesh() = nullptr;
		}
		
		for (auto& primitive : primitives)
		{
			primitive.uniformBuffer.Destroy();
		}
		vertices.clear();
		vertices.shrink_to_fit();
		indices.clear();
		indices.shrink_to_fit();
		if (Pipeline::getDescriptorSetLayoutPrimitive())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutPrimitive());
			Pipeline::getDescriptorSetLayoutPrimitive() = nullptr;
		}
	}
}
