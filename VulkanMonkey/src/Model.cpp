#include "../include/Model.h"
#include "../include/Errors.h"
#include "../include/assimp/Importer.hpp"      // C++ importer interface
#include "../include/assimp/scene.h"           // Output data structure
#include "../include/assimp/postprocess.h"     // Post processing flags
#include "../include/assimp/DefaultLogger.hpp"

using namespace vm;

vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;

Model::Model(VulkanContext * vulkan) : vulkan(vulkan)
{ }

vm::mat4 aiMatrix4x4ToMat4(const aiMatrix4x4& m)
{
	return vm::transpose(vm::mat4((float*)&m));
}

void getAllNodes(aiNode* root, std::vector<aiNode*>& allNodes) {
	for (uint32_t i = 0; i < root->mNumChildren; i++)
		getAllNodes(root->mChildren[i], allNodes);
	if (root) allNodes.push_back(root);
}

void Model::loadModel(const std::string path, const std::string modelName, bool show)
{
	// Materials, Vertices and Indices load
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path + modelName,
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

	std::vector<aiNode*> allNodes{};
	getAllNodes(scene->mRootNode, allNodes);

	std::vector<Mesh> f_meshes;
	for (unsigned int n = 0; n < allNodes.size(); n++) {
		const aiNode& node = *allNodes[n];
		const vm::mat4 transform = aiMatrix4x4ToMat4(node.mTransformation);
		for (unsigned int i = 0; i < node.mNumMeshes; i++) {
			Mesh myMesh = Mesh(vulkan);

			const aiMesh& mesh = *scene->mMeshes[node.mMeshes[i]];
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

			aiString aitexPath;
			material.GetTexture(aiTextureType_DIFFUSE, 0, &aitexPath);
			std::string texPath = aitexPath.C_Str();
			if (texPath != "")	texPath = path + texPath;
			else				texPath = "objects/default.png";
			if (uniqueTextures.find(texPath) != uniqueTextures.end()) {
				myMesh.texture = uniqueTextures[texPath];
			}
			else {
				myMesh.loadTexture(Mesh::DiffuseMap, texPath);
				uniqueTextures[texPath] = myMesh.texture;
			}

			aiString aiNormTexPath;
			material.GetTexture(aiTextureType_HEIGHT, 0, &aiNormTexPath);
			std::string normTexPath = aiNormTexPath.C_Str();
			if (normTexPath != "")	normTexPath = path + normTexPath;
			else					normTexPath = "objects/defaultNormalMap.png";
			if (uniqueTextures.find(normTexPath) != uniqueTextures.end()) {
				myMesh.normalsTexture = uniqueTextures[normTexPath];
			}
			else {
				myMesh.loadTexture(Mesh::NormalMap, normTexPath);
				uniqueTextures[normTexPath] = myMesh.normalsTexture;
			}

			aiString aiSpecTexPath;
			material.GetTexture(aiTextureType_SPECULAR, 0, &aiSpecTexPath);
			std::string specTexPath = aiSpecTexPath.C_Str();
			if (specTexPath != "")	specTexPath = path + specTexPath;
			else					specTexPath = "objects/defaultSpecularMap.png";
			if (uniqueTextures.find(specTexPath) != uniqueTextures.end()) {
				myMesh.specularTexture = uniqueTextures[specTexPath];
			}
			else {
				myMesh.loadTexture(Mesh::SpecularMap, specTexPath);
				uniqueTextures[specTexPath] = myMesh.specularTexture;
			}

			aiString aiAlphaTexPath;
			material.GetTexture(aiTextureType_OPACITY, 0, &aiAlphaTexPath);
			std::string aplhaTexPath = aiAlphaTexPath.C_Str();
			if (aplhaTexPath != "")	aplhaTexPath = path + aplhaTexPath;
			else					aplhaTexPath = "objects/default.png";
			if (uniqueTextures.find(aplhaTexPath) != uniqueTextures.end()) {
				myMesh.alphaTexture = uniqueTextures[aplhaTexPath];
			}
			else {
				myMesh.loadTexture(Mesh::AlphaMap, aplhaTexPath);
				uniqueTextures[aplhaTexPath] = myMesh.alphaTexture;
			}
			for (unsigned int j = 0; j < mesh.mNumVertices; j++) {
				const aiVector3D& pos = mesh.HasPositions() ? mesh.mVertices[j] : aiVector3D(0.f, 0.f, 0.f);
				const aiVector3D& norm = mesh.HasNormals() ? mesh.mNormals[j] : aiVector3D(0.f, 0.f, 0.f);
				const aiVector3D& uv = mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][j] : aiVector3D(0.f, 0.f, 0.f);
				const aiVector3D& tangent = mesh.HasTangentsAndBitangents() ? mesh.mTangents[j] : aiVector3D(0.f, 0.f, 0.f);
				const aiVector3D& bitangent = mesh.HasTangentsAndBitangents() ? mesh.mBitangents[j] : aiVector3D(0.f, 0.f, 0.f);
				const aiColor4D& color = mesh.HasVertexColors(0) ? mesh.mColors[0][j] : aiColor4D(1.f, 1.f, 1.f, 1.f);

				vm::vec4 p = transform * vm::vec4(pos.x, pos.y, pos.z, 1.f);
				vm::vec4 n = transform * vm::vec4(norm.x, norm.y, norm.z, 1.f);
				vm::vec4 t = transform * vm::vec4(tangent.x, tangent.y, tangent.z, 1.f);
				vm::vec4 b = transform * vm::vec4(bitangent.x, bitangent.y, bitangent.z, 1.f);
				Vertex v(
					vm::vec3(p),
					vm::vec2((float*)&uv),
					vm::vec3(n),
					vm::vec3(t),
					vm::vec3(b),
					vm::vec4((float*)&color)
				);
				myMesh.vertices.push_back(v);
			}
			for (unsigned int i = 0; i < mesh.mNumFaces; i++) {
				const aiFace& Face = mesh.mFaces[i];
				assert(Face.mNumIndices == 3);
				myMesh.indices.push_back(Face.mIndices[0]);
				myMesh.indices.push_back(Face.mIndices[1]);
				myMesh.indices.push_back(Face.mIndices[2]);
			}
			myMesh.calculateBoundingSphere();
			f_meshes.push_back(myMesh);
		}
	}
	std::sort(f_meshes.begin(), f_meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.hasAlpha < b.hasAlpha; });
	for (auto &m : f_meshes) {
		m.vertexOffset = numberOfVertices;
		m.indexOffset = numberOfIndices;

		meshes.push_back(m);
		numberOfVertices += static_cast<uint32_t>(m.vertices.size());
		numberOfIndices += static_cast<uint32_t>(m.indices.size());
	}

	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorSets();
	name = modelName;
	render = show;

	// resizing the model to always be at a certain magnitude
	float scale = 2.0f / getBoundingSphere().w;
	matrix = vm::scale(matrix, vm::vec3(scale, scale, scale));
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
					const vk::DescriptorSet descriptorSets[2] = { descriptorSet, mesh.descriptorSet };
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 2, descriptorSets, 0, nullptr);
				}
				else {
					const uint32_t dOffsets[] = { modelID * sizeof(ShadowsUBO) };
					const vk::DescriptorSet descriptorSets[4] = { shadows->descriptorSet, mesh.descriptorSet, descriptorSet, *DSLights };
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 4, descriptorSets, 1, dOffsets);
				}
				cmd.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
			}
		}
	}
}

// position x, y, z and radius w
vm::vec4 Model::getBoundingSphere()
{

	//vm::vec3 center = (float*)&(matrix * vm::vec4(0.0f, 0.0f, 0.0f, 1.f));
	//vm::vec3 temp = (float*)&(matrix * vm::vec4(1.0f, 1.0f, 1.0f, 1.f));
	//vm::vec3 tranformation = temp - center;
	vm::vec3 center = vm::vec3(0.0f, 0.0f, 0.0f);

	if (initialBoundingSphereRadius <= 0) {
		for (auto &mesh : meshes) {
			for (auto& vertex : mesh.vertices) {
				float distance = vm::length(vm::vec3(vertex.x, vertex.y, vertex.z));
				if (distance > initialBoundingSphereRadius)
					initialBoundingSphereRadius = distance;
			}
		}
	}

	return vm::vec4(center, initialBoundingSphereRadius);
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
		VkCheck(device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
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
	Buffer staging = Buffer(vulkan);
	staging.createBuffer(sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	VkCheck(vulkan->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
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
	Buffer staging = Buffer(vulkan);
	staging.createBuffer(sizeof(uint32_t)*indices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	VkCheck(vulkan->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, indices.data(), sizeof(uint32_t)*indices.size());
	vulkan->device.unmapMemory(staging.memory);

	indexBuffer.copyBuffer(staging.buffer, staging.size);
	staging.destroy();
}

void Model::createUniformBuffers()
{
	// since the uniform buffers are unique for each model, they are not bigger than 256 in size
	uniformBuffer.createBuffer(256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Model::createDescriptorSets()
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VkCheck(vulkan->device.allocateDescriptorSets(&allocateInfo, &descriptorSet));

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

	vulkan->device.updateDescriptorSets(1, &mvpWriteSet, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

	for (auto& mesh : meshes) {
		auto const allocateInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(vulkan->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&mesh.descriptorSetLayout);
		VkCheck(vulkan->device.allocateDescriptorSets(&allocateInfo, &mesh.descriptorSet));

		// Texture
		vk::WriteDescriptorSet textureWriteSets[4];

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

		vulkan->device.updateDescriptorSets(4, textureWriteSets, 0, nullptr);
		std::cout << "DescriptorSet allocated and updated\n";
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

	for (auto& texture : uniqueTextures)
		texture.second.destroy();

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

	std::cout << "Model and associated structs destroyed\n";
}