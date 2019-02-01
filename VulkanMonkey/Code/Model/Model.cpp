#include "Model.h"
#include "../../include/assimp/Importer.hpp"      // C++ importer interface
#include "../../include/assimp/scene.h"           // Output data structure
#include "../../include/assimp/postprocess.h"     // Post processing flags
#include "../../include/assimp/DefaultLogger.hpp"
#include "../../include/assimp/pbrmaterial.h"

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

bool endsWith(const std::string &mainStr, const std::string &toMatch)
{
	if (mainStr.size() >= toMatch.size() &&
		mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
		return true;
	else
		return false;
}

void Model::loadModel(const std::string& folderPath, const std::string& modelName, bool show)
{
	bool gltfModel = false;
	if (endsWith(modelName, ".gltf"))
		gltfModel = true;
	// Materials, Vertices and Indices load
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(folderPath + modelName,
		//aiProcess_MakeLeftHanded |
		//aiProcess_FlipUVs |
		//aiProcess_FlipWindingOrder |
		aiProcess_ConvertToLeftHanded |
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
	for (auto& node : allNodes) {
		mat4 transform = getTranform(*node);
		for (unsigned int i = 0; i < node->mNumMeshes; i++) {
			Mesh myMesh;
			myMesh.transform = transform;

			const aiMesh& mesh = *scene->mMeshes[node->mMeshes[i]];
			for (unsigned int j = 0; j < mesh.mNumVertices; j++) {
				myMesh.vertices.push_back({
					vec3(reinterpret_cast<float*>(&(mesh.HasPositions() ? mesh.mVertices[j] : aiVector3D(0.f, 0.f, 0.f)))),
					vec2(reinterpret_cast<float*>(&(mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][j] : aiVector3D(0.f, 0.f, 0.f)))),
					vec3(reinterpret_cast<float*>(&(mesh.HasNormals() ? mesh.mNormals[j] : aiVector3D(0.f, 0.f, 0.f)))),
					vec3(reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mTangents[j] : aiVector3D(0.f, 0.f, 0.f)))),
					vec3(reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mBitangents[j] : aiVector3D(0.f, 0.f, 0.f)))),
					vec4(reinterpret_cast<float*>(&(mesh.HasVertexColors(0) ? mesh.mColors[0][j] : aiColor4D(1.f, 1.f, 1.f, 1.f))))
					});
			}
			for (unsigned int n = 0; n < mesh.mNumFaces; n++) {
				const aiFace& Face = mesh.mFaces[n];
				assert(Face.mNumIndices == 3);
				myMesh.indices.push_back(Face.mIndices[0]);
				myMesh.indices.push_back(Face.mIndices[1]);
				myMesh.indices.push_back(Face.mIndices[2]);
			}

			const aiMaterial& material = *scene->mMaterials[mesh.mMaterialIndex];
			// factors
			material.Get(AI_MATKEY_COLOR_DIFFUSE, *reinterpret_cast<aiColor3D*>(&myMesh.material.colorDiffuse));
			material.Get(AI_MATKEY_COLOR_SPECULAR, *reinterpret_cast<aiColor3D*>(&myMesh.material.colorSpecular));
			material.Get(AI_MATKEY_COLOR_AMBIENT, *reinterpret_cast<aiColor3D*>(&myMesh.material.colorAmbient));
			material.Get(AI_MATKEY_COLOR_EMISSIVE, *reinterpret_cast<aiColor3D*>(&myMesh.material.colorEmissive));
			material.Get(AI_MATKEY_COLOR_TRANSPARENT, *reinterpret_cast<aiColor3D*>(&myMesh.material.colorTransparent));
			material.Get(AI_MATKEY_ENABLE_WIREFRAME, myMesh.material.wireframe);
			material.Get(AI_MATKEY_TWOSIDED, myMesh.material.twoSided);
			material.Get(AI_MATKEY_SHADING_MODEL, myMesh.material.shadingModel);
			material.Get(AI_MATKEY_BLEND_FUNC, myMesh.material.blendFunc);
			material.Get(AI_MATKEY_OPACITY, myMesh.material.opacity);
			material.Get(AI_MATKEY_SHININESS, myMesh.material.shininess);
			material.Get(AI_MATKEY_SHININESS_STRENGTH, myMesh.material.shininessStrength);
			material.Get(AI_MATKEY_REFRACTI, myMesh.material.refraction);
			// textures
			myMesh.loadTexture(Mesh::DiffuseMap, folderPath, getTextureName(material, aiTextureType_DIFFUSE));
			myMesh.loadTexture(Mesh::SpecularMap, folderPath, getTextureName(material, aiTextureType_SPECULAR));
			myMesh.loadTexture(Mesh::AmbientMap, folderPath, getTextureName(material, aiTextureType_AMBIENT)); // metalic
			myMesh.loadTexture(Mesh::EmissiveMap, folderPath, getTextureName(material, aiTextureType_EMISSIVE));
			myMesh.loadTexture(Mesh::HeightMap, folderPath, getTextureName(material, aiTextureType_HEIGHT)); // normals
			myMesh.loadTexture(Mesh::NormalsMap, folderPath, getTextureName(material, aiTextureType_NORMALS));
			myMesh.loadTexture(Mesh::ShininessMap, folderPath, getTextureName(material, aiTextureType_SHININESS)); // roughness
			myMesh.loadTexture(Mesh::OpacityMap, folderPath, getTextureName(material, aiTextureType_OPACITY));
			myMesh.loadTexture(Mesh::DisplacementMap, folderPath, getTextureName(material, aiTextureType_DISPLACEMENT));
			myMesh.loadTexture(Mesh::LightMap, folderPath, getTextureName(material, aiTextureType_LIGHTMAP));
			myMesh.loadTexture(Mesh::ReflectionMap, folderPath, getTextureName(material, aiTextureType_REFLECTION));
			if (gltfModel) {
				// factors
				material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, *reinterpret_cast<aiColor4D*>(&myMesh.gltfMaterial.baseColorFactor));
				material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, myMesh.gltfMaterial.metallicFactor);
				material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, myMesh.gltfMaterial.roughnessFactor);
				std::swap(myMesh.gltfMaterial.emissiveFactor, myMesh.material.colorEmissive);
				material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, myMesh.gltfMaterial.alphaCutoff);
				std::swap(myMesh.gltfMaterial.doubleSided, myMesh.material.twoSided);
				// textures
				std::swap(myMesh.gltfMaterial.baseColorTexture, myMesh.material.textureDiffuse);
				myMesh.loadTexture(Mesh::MetallicRoughness, folderPath, getTextureName(material, aiTextureType_UNKNOWN));
				std::swap(myMesh.gltfMaterial.normalTexture, myMesh.material.textureNormals);
				std::swap(myMesh.gltfMaterial.occlusionTexture, myMesh.material.textureLight);
				std::swap(myMesh.gltfMaterial.emissiveTexture, myMesh.material.textureEmissive);
			}
			meshes.push_back(myMesh);
		}
	}
	// Free assimp resources
	importer.FreeScene();

	std::sort(meshes.begin(), meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.hasAlphaMap < b.hasAlphaMap; });
	float factor = 1.0f;// 20.f / getBoundingSphere().w;
	for (auto &m : meshes) {
		for (auto& v : m.vertices) {
			v.position = m.transform * vec4(v.position, 1.f) * factor;
			v.normals = normalize(m.transform * vec4(v.normals, 0.f));
			v.tangents = normalize(m.transform * vec4(v.tangents, 0.f));
			v.bitangents = normalize(m.transform * vec4(v.bitangents, 0.f));
		}
		m.calculateBoundingSphere();
		m.vertexOffset = numberOfVertices;
		m.indexOffset = numberOfIndices;
		numberOfVertices += static_cast<uint32_t>(m.vertices.size());
		numberOfIndices += static_cast<uint32_t>(m.indices.size());
	}

	name = modelName;
	render = show;
	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorSets();
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

void Model::update(Camera& camera, float delta)
{
	//render = GUI::render_models;
	if (render) {
		Transform trans;
		if (script) {
			script->update(delta);
			script->getValue(trans, "transform");
		}
		mat4 pvm[4];
		pvm[0] = camera.projection;
		pvm[1] = camera.view;
		pvm[2] = trans.matrix() * transform;
		camera.ExtractFrustum(pvm[2]);
		for (auto &mesh : meshes) {
			mesh.cull = !camera.SphereInFrustum(mesh.boundingSphere);
			if (!mesh.cull) {
				pvm[3][0] = mesh.gltfMaterial.baseColorFactor;
				pvm[3][1] = vec4(mesh.gltfMaterial.emissiveFactor, 1.f);
				pvm[3][2] = vec4(mesh.gltfMaterial.metallicFactor, mesh.gltfMaterial.roughnessFactor, mesh.gltfMaterial.alphaCutoff, mesh.hasAlphaMap ? 1.f : 0.f);
				memcpy(uniformBuffer.data, &pvm, sizeof(pvm));
			}
		}
	}
}

// position x, y, z and radius w
vec4 Model::getBoundingSphere()
{
	for (auto& mesh : meshes) {
		for (auto& vertex : mesh.vertices) {
			float temp = length(vertex.position);
			if (temp > boundingSphere.w)
				boundingSphere.w = temp;
		}
	}
	return boundingSphere; // unscaled bounding sphere with 0,0,0 origin
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

	bool gltfModel = false;
	if (endsWith(name, ".gltf"))
		gltfModel = true;

	for (auto& mesh : meshes) {
		auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(vulkan->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&mesh.descriptorSetLayout);
		mesh.descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo2)[0];

		// Texture
		std::vector<vk::WriteDescriptorSet> textureWriteSets(6);

		Image& baseColor = gltfModel ? mesh.gltfMaterial.baseColorTexture : mesh.material.textureDiffuse;
		Image& normals = gltfModel ? mesh.gltfMaterial.normalTexture : mesh.material.textureHeight;
		Image& specORroughMetal = gltfModel ? mesh.gltfMaterial.metallicRoughnessTexture : mesh.material.textureSpecular;
		Image& opacity = mesh.material.textureOpacity;
		Image& shininessOremissive = gltfModel ? mesh.gltfMaterial.emissiveTexture : mesh.material.textureShininess;
		Image& ambientORao = gltfModel ? mesh.gltfMaterial.occlusionTexture : mesh.material.textureAmbient;

		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(baseColor.sampler)									// Sampler sampler;
				.setImageView(baseColor.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[1] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(1)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(normals.sampler)									// Sampler sampler;
				.setImageView(normals.view)										// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[2] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(2)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(specORroughMetal.sampler)							// Sampler sampler;
				.setImageView(specORroughMetal.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[3] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(3)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(opacity.sampler)								// Sampler sampler;
				.setImageView(opacity.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[4] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(4)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(shininessOremissive.sampler)						// Sampler sampler;
				.setImageView(shininessOremissive.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[5] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(5)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(ambientORao.sampler)								// Sampler sampler;
				.setImageView(ambientORao.view)									// ImageView imageView;
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