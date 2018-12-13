#include "Model.h"
#include "../../include/assimp/Importer.hpp"      // C++ importer interface
#include "../../include/assimp/scene.h"           // Output data structure
#include "../../include/assimp/postprocess.h"     // Post processing flags
#include "../../include/assimp/DefaultLogger.hpp"

using namespace vm;

vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;

mat4 aiMatrix4x4ToMat4(const aiMatrix4x4& m)
{
	return transpose(mat4((float*)&m));
}

void getNodes(aiNode* root, std::vector<aiNode*>& allNodes)
{
	for (uint32_t i = 0; i < root->mNumChildren; i++)
		getNodes(root->mChildren[i], allNodes);
	if (root) allNodes.push_back(root);
}

std::vector<aiNode*> getAllNodes(aiNode* root)
{
	std::vector<aiNode*> allNodes;
	getNodes(root, allNodes);
	return allNodes;
}

mat4 getTranform(aiNode& node)
{
	mat4 transform = aiMatrix4x4ToMat4(node.mTransformation);
	aiNode* tranformNode = &node;
	while (tranformNode->mParent) {
		transform = aiMatrix4x4ToMat4(tranformNode->mParent->mTransformation) * transform;
		tranformNode = tranformNode->mParent;
	}
	return transform;
}

std::string getTextureName(const aiMaterial& material, const aiTextureType& type)
{
	aiString aiTexName;
	material.GetTexture(type, 0, &aiTexName);
	return aiTexName.C_Str();
}

void Model::loadModel(const std::string folderPath, const std::string modelName, bool show)
{
	// Materials, Vertices and Indices load
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(folderPath + modelName,
		//aiProcess_MakeLeftHanded |
		aiProcess_FlipUVs |
		//aiProcess_FlipWindingOrder |
		//aiProcess_ConvertToLeftHanded |
		aiProcess_JoinIdenticalVertices |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		//aiProcess_ImproveCacheLocality |
		//aiProcess_OptimizeMeshes |
		//aiProcess_OptimizeGraph |
		0
	);
	if (!scene) exit(-100);

	std::vector<aiNode*> allNodes = getAllNodes(scene->mRootNode);

	// for each different type it will search for the corresponding material in mesh if possible
	std::vector<std::tuple<aiTextureType, Mesh::TextureType>> textureType
	{
		{aiTextureType_SHININESS, Mesh::RoughnessMap},
		{aiTextureType_AMBIENT, Mesh::MetallicMap},
		{aiTextureType_DIFFUSE, Mesh::DiffuseMap},
		{aiTextureType_NORMALS, Mesh::NormalMap},
		{aiTextureType_SPECULAR, Mesh::SpecularMap},
		{aiTextureType_OPACITY, Mesh::AlphaMap}
	};

	for (auto& node : allNodes) {
		mat4 transform = getTranform(*node);
		for (unsigned int i = 0; i < node->mNumMeshes; i++) {
			Mesh myMesh;
			myMesh.transform = transform;

			const aiMesh& mesh = *scene->mMeshes[node->mMeshes[i]];
			const aiMaterial& material = *scene->mMaterials[mesh.mMaterialIndex];

			aiColor3D aiAmbient(0.f, 0.f, 0.f);
			material.Get(AI_MATKEY_COLOR_AMBIENT, aiAmbient);
			myMesh.colorEffects.ambient = { aiAmbient.r, aiAmbient.g, aiAmbient.b, 0.f };

			aiColor3D aiDiffuse(1.f, 1.f, 1.f);
			material.Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuse);
			float aiOpacity = 1.f;
			material.Get(AI_MATKEY_OPACITY, aiOpacity);
			myMesh.colorEffects.diffuse = { aiDiffuse.r, aiDiffuse.g, aiDiffuse.b, aiOpacity };

			aiColor3D aiSpecular(0.f, 0.f, 0.f);
			material.Get(AI_MATKEY_COLOR_SPECULAR, aiSpecular);
			myMesh.colorEffects.specular = { aiSpecular.r, aiSpecular.g, aiSpecular.b, 100.f };

			// texture maps loading
			for (auto& type : textureType)
				myMesh.loadTexture(std::get<1>(type), folderPath, getTextureName(material, std::get<0>(type)));

			for (unsigned int j = 0; j < mesh.mNumVertices; j++) {
				myMesh.vertices.push_back({
					reinterpret_cast<float*>(&(mesh.HasPositions() ? mesh.mVertices[j] : aiVector3D(0.f, 0.f, 0.f))),
					reinterpret_cast<float*>(&(mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][j] : aiVector3D(0.f, 0.f, 0.f))),
					reinterpret_cast<float*>(&(mesh.HasNormals() ? mesh.mNormals[j] : aiVector3D(0.f, 0.f, 0.f))),
					reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mTangents[j] : aiVector3D(0.f, 0.f, 0.f))),
					reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mBitangents[j] : aiVector3D(0.f, 0.f, 0.f))),
					reinterpret_cast<float*>(&(mesh.HasVertexColors(0) ? mesh.mColors[0][j] : aiColor4D(1.f, 1.f, 1.f, 1.f)))
				});
			}
			for (unsigned int i = 0; i < mesh.mNumFaces; i++) {
				const aiFace& Face = mesh.mFaces[i];
				assert(Face.mNumIndices == 3);
				myMesh.indices.push_back(Face.mIndices[0]);
				myMesh.indices.push_back(Face.mIndices[1]);
				myMesh.indices.push_back(Face.mIndices[2]);
			}
			meshes.push_back(myMesh);
		}
	}
	std::sort(meshes.begin(), meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.hasAlpha < b.hasAlpha; });
	float factor = 20.f / getBoundingSphere().w;
	for (auto &m : meshes) {
		for (auto& v : m.vertices) {
			vec3 pos = m.transform * vec4(v.x, v.y, v.z, 1.f) * factor;
			vec3 norm = m.transform * vec4(v.nX, v.nY, v.nZ, 0.f);
			vec3 tang = m.transform * vec4(v.tX, v.tY, v.tZ, 0.f);
			vec3 btang = m.transform * vec4(v.bX, v.bY, v.bZ, 0.f);
			v.x = pos.x;
			v.y = pos.y;
			v.z = pos.z;
			v.nX = norm.x;
			v.nY = norm.y;
			v.nZ = norm.z;
			v.tX = tang.x;
			v.tY = tang.y;
			v.tZ = tang.z;
			v.bX = btang.x;
			v.bY = btang.y;
			v.bZ = btang.z;
		}
		m.calculateBoundingSphere();
		m.vertexOffset = numberOfVertices;
		m.indexOffset = numberOfIndices;
		numberOfVertices += static_cast<uint32_t>(m.vertices.size());
		numberOfIndices += static_cast<uint32_t>(m.indices.size());
	}

	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorSets();
	name = modelName;
	render = show;
}

void Model::draw(Pipeline& pipeline, vk::CommandBuffer& cmd, const uint32_t& modelID, bool deferredRenderer, Shadows* shadows, vk::DescriptorSet* DSLights)
{
	if (render)
	{
		const vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);

		cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer, &offset);
		cmd.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint32);

		for (auto& mesh : meshes) {
			if (mesh.render && !mesh.cull) {
				if (deferredRenderer) {
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, { descriptorSet, mesh.descriptorSet }, nullptr);
				}
				else {
					const uint32_t dOffsets = modelID * sizeof(ShadowsUBO);
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, { shadows->descriptorSet, mesh.descriptorSet, descriptorSet, *DSLights }, dOffsets);
				}
				cmd.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
			}
		}
	}
}

// position x, y, z and radius w
vec4 Model::getBoundingSphere()
{
	for (auto& mesh : meshes) {
		for (auto& vertex : mesh.vertices) {
			float temp = length(vec3(vertex.x, vertex.y, vertex.z));
			if (temp > boundingSphere.w)
				boundingSphere.w = temp;
		}
	}
	return boundingSphere;
}

vk::DescriptorSetLayout Model::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

void Model::createVertexBuffer()
{
	std::vector<Vertex> vertices{};
	for (auto& mesh : meshes) {
		for (auto& vertex : mesh.vertices)
			vertices.push_back(vertex);
	}

	vertexBuffer.createBuffer(sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	staging.data = vulkan->device.mapMemory(staging.memory, 0, staging.size);
	memcpy(staging.data, vertices.data(), sizeof(Vertex)*vertices.size());
	vulkan->device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(staging.buffer, staging.size);
	staging.destroy();
}

void Model::createIndexBuffer()
{
	std::vector<uint32_t> indices{};
	for (auto& mesh : meshes) {
		for (auto& index : mesh.indices)
			indices.push_back(index);
	}
	indexBuffer.createBuffer(sizeof(uint32_t)*indices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(sizeof(uint32_t)*indices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	staging.data = vulkan->device.mapMemory(staging.memory, 0, staging.size);
	memcpy(staging.data, indices.data(), sizeof(uint32_t)*indices.size());
	vulkan->device.unmapMemory(staging.memory);

	indexBuffer.copyBuffer(staging.buffer, staging.size);
	staging.destroy();
}

void Model::createUniformBuffers()
{
	// since the uniform buffers are unique for each model, they are not bigger than 256 in size
	uniformBuffer.createBuffer(256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);
}

void Model::createDescriptorSets()
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo)[0];

	// Model MVP
	auto const mvpWriteSet = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)								// DescriptorSet dstSet;
		.setDstBinding(0)										// uint32_t dstBinding;
		.setDstArrayElement(0)									// uint32_t dstArrayElement;
		.setDescriptorCount(1)									// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)						// Buffer buffer;
			.setOffset(0)											// DeviceSize offset;
			.setRange(uniformBuffer.size));							// DeviceSize range;

	vulkan->device.updateDescriptorSets(mvpWriteSet, nullptr);

	for (auto& mesh : meshes) {
		auto const allocateInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(vulkan->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&mesh.descriptorSetLayout);
		mesh.descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo)[0];

		// Texture
		std::vector<vk::WriteDescriptorSet> textureWriteSets(6);

		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.texture.sampler)								// Sampler sampler;
				.setImageView(mesh.texture.view)								// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[1] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(1)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.normalsTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.normalsTexture.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[2] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(2)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.specularTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.specularTexture.view)						// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[3] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(3)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.alphaTexture.sampler)							// Sampler sampler;
				.setImageView(mesh.alphaTexture.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[4] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(4)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.roughnessTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.roughnessTexture.view)						// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[5] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(5)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.metallicTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.metallicTexture.view)						// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
	}
}

void Model::destroy()
{
	for (auto& mesh : meshes) {
		mesh.vertices.clear();
		mesh.vertices.shrink_to_fit();
		mesh.indices.clear();
		mesh.indices.shrink_to_fit();
	}

	for (auto& texture : Mesh::uniqueTextures)
		texture.second.destroy();
	Mesh::uniqueTextures.clear();

	vertexBuffer.destroy();
	indexBuffer.destroy();
	uniformBuffer.destroy();

	if (Model::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
		Model::descriptorSetLayout = nullptr;
	}

	if (Mesh::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Mesh::descriptorSetLayout);
		Mesh::descriptorSetLayout = nullptr;
	}
}