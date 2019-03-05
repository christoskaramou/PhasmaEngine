#include "Model.h"
#include "../GUI/GUI.h"

using namespace vm;

vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;
std::vector<Model> Model::models{};
Pipeline* Model::pipeline = nullptr;

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
	for (auto& m : models) {
		if (m.name == modelName) {
			animation = m.animation;
			animation.runningTimeSeconds = 0.f;
			importers = m.importers;
			scene = m.scene;
			transform = m.transform;
			isCopy = true;
			name = m.name;
			render = show;
			vertexBuffer = m.vertexBuffer;
			indexBuffer = m.indexBuffer;
			meshes = m.meshes;
			createUniformBuffers();
			createDescriptorSets();
			return;
		}
	}

	//bool gltfModel = false;
	//if (endsWith(modelName, ".gltf"))
	//	gltfModel = true;
	// Materials, Vertices and Indices load
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	importers.push_back(new Assimp::Importer());
	scene = importers.back()->ReadFile(folderPath + modelName,
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

	animation.scene = scene;
	animation.numAnimations = scene->mNumAnimations;
	animation.setAnimation(0);

	// Setup bones
	// One vertex bone info structure per vertex
	uint32_t vertexCount(0);
	for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
		vertexCount += scene->mMeshes[m]->mNumVertices;
	};
	animation.bones.resize(vertexCount);
	// Store global inverse transform matrix of root node 
	animation.globalInverseTransform = scene->mRootNode->mTransformation;
	transform = aiMatrix4x4ToMat4(animation.globalInverseTransform);
	animation.globalInverseTransform.Inverse();

	// Load bones (weights and IDs)
	uint32_t vertexBase(0);
	for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
		aiMesh *paiMesh = scene->mMeshes[m];
		if (paiMesh->mNumBones > 0) {
			animation.loadBones(paiMesh, vertexBase, animation.bones);
		}
		vertexBase += scene->mMeshes[m]->mNumVertices;
	}

	meshes.reserve(scene->mNumMeshes);
	for (unsigned int i = 0; i < scene->mNumMeshes; i++) {

		const aiMesh& mesh = *scene->mMeshes[i];

		Mesh myMesh;
		//myMesh.transform = transform;
		myMesh.hasBones = mesh.HasBones();
		myMesh.vertexOffset = numberOfVertices;
		myMesh.indexOffset = numberOfIndices;
		numberOfVertices += mesh.mNumVertices;
		numberOfIndices += mesh.mNumFaces * 3;


		// Vertices
		myMesh.vertices.reserve(mesh.mNumVertices);
		for (unsigned int j = 0; j < mesh.mNumVertices; j++) {
			Vertex vertex;
			vertex.position = vec3(reinterpret_cast<float*>(&(mesh.HasPositions() ? mesh.mVertices[j] : aiVector3D(0.f, 0.f, 0.f))));
			vertex.uv = vec2(reinterpret_cast<float*>(&(mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][j] : aiVector3D(0.f, 0.f, 0.f))));
			vertex.normals = vec3(reinterpret_cast<float*>(&(mesh.HasNormals() ? mesh.mNormals[j] : aiVector3D(0.f, 0.f, 0.f))));
			vertex.tangents = vec3(reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mTangents[j] : aiVector3D(0.f, 0.f, 0.f))));
			vertex.bitangents = vec3(reinterpret_cast<float*>(&(mesh.HasTangentsAndBitangents() ? mesh.mBitangents[j] : aiVector3D(0.f, 0.f, 0.f))));
			vertex.color = vec4(reinterpret_cast<float*>(&(mesh.HasVertexColors(0) ? mesh.mColors[0][j] : aiColor4D(0.f, 0.f, 0.f, 0.f))));
			if (myMesh.hasBones) {
				for (uint32_t b = 0; b < MAX_BONES_PER_VERTEX; b++) {
					vertex.weights[b] = animation.bones[myMesh.vertexOffset + j].weights[b];
					vertex.bonesIDs[b] = animation.bones[myMesh.vertexOffset + j].IDs[b];
				}
			}
			myMesh.vertices.push_back(vertex);
		}

		// Indices
		myMesh.indices.reserve(mesh.mNumFaces);
		for (unsigned int n = 0; n < mesh.mNumFaces; n++) {
			const aiFace& Face = mesh.mFaces[n];
			assert(Face.mNumIndices == 3);
			myMesh.indices.push_back(Face.mIndices[0]);
			myMesh.indices.push_back(Face.mIndices[1]);
			myMesh.indices.push_back(Face.mIndices[2]);
		}

		// Materials
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
		myMesh.loadTexture(TextureType::DiffuseMap, folderPath, getTextureName(material, aiTextureType_DIFFUSE));
		myMesh.loadTexture(TextureType::SpecularMap, folderPath, getTextureName(material, aiTextureType_SPECULAR));
		myMesh.loadTexture(TextureType::AmbientMap, folderPath, getTextureName(material, aiTextureType_AMBIENT)); // metalic
		myMesh.loadTexture(TextureType::EmissiveMap, folderPath, getTextureName(material, aiTextureType_EMISSIVE));
		myMesh.loadTexture(TextureType::HeightMap, folderPath, getTextureName(material, aiTextureType_HEIGHT)); // normals
		myMesh.loadTexture(TextureType::NormalsMap, folderPath, getTextureName(material, aiTextureType_NORMALS));
		myMesh.loadTexture(TextureType::ShininessMap, folderPath, getTextureName(material, aiTextureType_SHININESS)); // roughness
		myMesh.loadTexture(TextureType::OpacityMap, folderPath, getTextureName(material, aiTextureType_OPACITY));
		myMesh.loadTexture(TextureType::DisplacementMap, folderPath, getTextureName(material, aiTextureType_DISPLACEMENT));
		myMesh.loadTexture(TextureType::LightMap, folderPath, getTextureName(material, aiTextureType_LIGHTMAP));
		myMesh.loadTexture(TextureType::ReflectionMap, folderPath, getTextureName(material, aiTextureType_REFLECTION));

		std::string metalicRoughnessName = getTextureName(material, aiTextureType_UNKNOWN);
		myMesh.hasPBR = metalicRoughnessName != "";

		// PBR -------------------------------------------------------
		myMesh.loadTexture(TextureType::MetallicRoughness, folderPath, metalicRoughnessName);
		std::swap(myMesh.pbrMaterial.baseColorTexture, myMesh.material.textureDiffuse);
		std::swap(myMesh.pbrMaterial.normalTexture, myMesh.material.textureNormals);
		std::swap(myMesh.pbrMaterial.occlusionTexture, myMesh.material.textureLight);
		std::swap(myMesh.pbrMaterial.emissiveTexture, myMesh.material.textureEmissive);

		material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, *reinterpret_cast<aiColor4D*>(&myMesh.pbrMaterial.baseColorFactor));
		material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, myMesh.pbrMaterial.metallicFactor);
		material.Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, myMesh.pbrMaterial.roughnessFactor);
		std::swap(myMesh.pbrMaterial.emissiveFactor, myMesh.material.colorEmissive);
		material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, myMesh.pbrMaterial.alphaCutoff);
		std::swap(myMesh.pbrMaterial.doubleSided, myMesh.material.twoSided);
		// -----------------------------------------------------------

		myMesh.calculateBoundingSphere();
		meshes.push_back(myMesh);
	}

	name = modelName;
	render = show;
	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorSets();
}

void vm::Model::batchStart(uint32_t imageIndex, Deferred& deferred)
{
	std::vector<vk::ClearValue> clearValues = {
	vk::ClearColorValue().setFloat32(GUI::clearColor),
	vk::ClearColorValue().setFloat32(GUI::clearColor),
	vk::ClearColorValue().setFloat32(GUI::clearColor),
	vk::ClearColorValue().setFloat32(GUI::clearColor),
	vk::ClearColorValue().setFloat32(GUI::clearColor),
	vk::ClearDepthStencilValue({ 0.0f, 0 }) };
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(deferred.renderPass)
		.setFramebuffer(deferred.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, VulkanContext::get().surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	VulkanContext::get().dynamicCmdBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	Model::pipeline = &deferred.pipeline;
}

void vm::Model::batchEnd()
{
	VulkanContext::get().dynamicCmdBuffer.endRenderPass();
	Model::pipeline = nullptr;
}

void Model::draw()
{
	if (!render) return;
	auto& cmd = vulkan->dynamicCmdBuffer;
	const vk::DeviceSize offset{ 0 };
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeline);
	cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer, &offset);
	cmd.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint32);

	for (auto& mesh : meshes) {
		if (mesh.render && !mesh.cull) {
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo.layout, 0, { descriptorSet, mesh.descriptorSet }, nullptr);
			cmd.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
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
		mat4 pvm[4 + MAX_BONES];
		pvm[0] = camera.projection;
		pvm[1] = camera.view;
		pvm[2] = trans.matrix() * transform;
		pvm[3] = previousTransform;
		camera.ExtractFrustum(pvm[2]);

		if (scene->HasAnimations()) {
			animation.bonesTransform(delta);
			for (uint32_t i = 0; i < animation.boneTransforms.size(); i++)
				pvm[i + 4] = aiMatrix4x4ToMat4(animation.boneTransforms[i]);
		}

		memcpy(uniformBuffer.data, pvm, sizeof(pvm));
		previousTransform = pvm[2];
		for (auto &mesh : meshes) {
			mesh.cull = !camera.SphereInFrustum(mesh.boundingSphere);
			if (!mesh.cull) {
				mat4 factors;
				factors[0] = mesh.pbrMaterial.baseColorFactor != vec4(0.f) ? mesh.pbrMaterial.baseColorFactor : vec4(1.f);
				factors[1] = vec4(mesh.pbrMaterial.emissiveFactor, 1.f);
				factors[2] = vec4(mesh.pbrMaterial.metallicFactor, mesh.pbrMaterial.roughnessFactor, mesh.pbrMaterial.alphaCutoff, mesh.hasAlphaMap ? 1.f : 0.f);
				factors[3][0] = (float)mesh.hasBones;
				memcpy(mesh.uniformBuffer.data, &factors, sizeof(factors));
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

vk::DescriptorSetLayout Model::getDescriptorSetLayout()
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
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(createInfo);
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
	size_t size = (4 + MAX_BONES) * sizeof(mat4);
	uniformBuffer.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);

	for (auto& mesh : meshes)
		mesh.createUniformBuffer();
}

void Model::createDescriptorSets()
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

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

	//bool gltfModel = false;
	//if (endsWith(name, ".gltf"))
	//	gltfModel = true;

	for (auto& mesh : meshes) {
		auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(vulkan->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&mesh.descriptorSetLayout);
		mesh.descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

		// Texture
		std::vector<vk::WriteDescriptorSet> textureWriteSets(7);

		//Image& baseColor = gltfModel ? mesh.pbrMaterial.baseColorTexture : mesh.material.textureDiffuse;
		//Image& normals = gltfModel ? mesh.pbrMaterial.normalTexture : mesh.material.textureHeight;
		//Image& specORroughMetal = gltfModel ? mesh.pbrMaterial.metallicRoughnessTexture : mesh.material.textureSpecular;
		//Image& opacity = mesh.material.textureOpacity;
		//Image& shininessOremissive = gltfModel ? mesh.pbrMaterial.emissiveTexture : mesh.material.textureShininess;
		//Image& ambientORao = gltfModel ? mesh.pbrMaterial.occlusionTexture : mesh.material.textureAmbient;

		Image& baseColor = mesh.pbrMaterial.baseColorTexture;
		Image& normals = mesh.pbrMaterial.normalTexture;
		Image& metallicRoughnes = mesh.pbrMaterial.metallicRoughnessTexture;
		Image& opacity = mesh.material.textureOpacity;
		Image& emissive = mesh.pbrMaterial.emissiveTexture;
		Image& occlusion = mesh.pbrMaterial.occlusionTexture;

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
				.setSampler(metallicRoughnes.sampler)							// Sampler sampler;
				.setImageView(metallicRoughnes.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[3] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(3)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(opacity.sampler)									// Sampler sampler;
				.setImageView(opacity.view)										// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[4] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(4)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(emissive.sampler)									// Sampler sampler;
				.setImageView(emissive.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[5] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(5)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(occlusion.sampler)									// Sampler sampler;
				.setImageView(occlusion.view)									// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[6] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)								// DescriptorSet dstSet;
			.setDstBinding(6)										// uint32_t dstBinding;
			.setDstArrayElement(0)									// uint32_t dstArrayElement;
			.setDescriptorCount(1)									// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
			.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(mesh.uniformBuffer.buffer)						// Buffer buffer;
				.setOffset(0)											// DeviceSize offset;
				.setRange(mesh.uniformBuffer.size));							// DeviceSize range;

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
		mesh.uniformBuffer.destroy();
	}

	for (auto& texture : Mesh::uniqueTextures)
		texture.second.destroy();
	Mesh::uniqueTextures.clear();

	if (!isCopy) {
		vertexBuffer.destroy();
		indexBuffer.destroy();
	}
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