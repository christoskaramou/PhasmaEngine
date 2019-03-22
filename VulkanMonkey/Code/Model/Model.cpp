#include "Model.h"
#include "../GUI/GUI.h"
#include "../../include/GLTFSDK/GLTF.h"
#include "../../include/GLTFSDK/GLTFResourceReader.h"
#include "../../include/GLTFSDK/GLBResourceReader.h"
#include "../../include/GLTFSDK/Deserialize.h"
#include "StreamReader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <future>

using namespace vm;
using namespace Microsoft;

vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;
std::vector<Model> Model::models{};
Pipeline* Model::pipeline = nullptr;

bool endsWith(const std::string &mainStr, const std::string &toMatch)
{
	if (mainStr.size() >= toMatch.size() &&
		mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
		return true;
	else
		return false;
}

void Model::readGltf(const std::filesystem::path& file)
{
	if (file.extension() != ".gltf" && file.extension() !=  ".glb")
		throw glTF::GLTFException("Model type not supported");

	std::string manifest;

	// Pass the absolute path, without the filename, to the stream reader
	auto streamReader = std::make_unique<StreamReader>(file.parent_path());
	std::filesystem::path pathFile = file.filename();
	// Pass a UTF-8 encoded filename to GetInputString
	auto gltfStream = streamReader->GetInputStream(pathFile.u8string());
	if (file.extension() == ".gltf") {
		resourceReader = new glTF::GLTFResourceReader(std::move(streamReader));
		// Read the contents of the glTF file into a std::stringstream
		std::stringstream manifestStream;
		manifestStream << gltfStream->rdbuf();
		manifest = manifestStream.str();
	}
	else {
		Microsoft::glTF::GLBResourceReader* resourceReaderGLB;
		resourceReaderGLB = new glTF::GLBResourceReader(std::move(streamReader), std::move(gltfStream));
		manifest = resourceReaderGLB->GetJson();
		resourceReader = resourceReaderGLB;
	}

	//std::cout << manifest;

	try
	{
		document = new Microsoft::glTF::Document();
		*document = glTF::Deserialize(manifest);
	}
	catch (const glTF::GLTFException& ex)
	{
		std::stringstream ss;

		ss << "Microsoft::glTF::Deserialize failed: ";
		ss << ex.what();

		throw std::runtime_error(ss.str());
	}
}

Microsoft::glTF::Image* Model::getImage(const std::string& textureID) {
	return textureID.empty() ? nullptr :
		const_cast<Microsoft::glTF::Image*>
		(&document->images.Get(document->textures.Get(textureID).imageId));
}

void Model::getMesh(vm::Node* node, const std::string& meshID, const std::string& folderPath)
{
	if (!node || meshID.empty()) return;
	const auto& mesh = document->meshes.Get(meshID);

	Mesh* myMesh = new Mesh();

	const glTF::Accessor* accessorPos;
	const glTF::Accessor* accessorTex;
	const glTF::Accessor* accessorNor;
	const glTF::Accessor* accessorCol;
	const glTF::Accessor* accessorJoi;
	const glTF::Accessor* accessorWei;
	const glTF::Accessor* accessorInd;

	for (const auto& primitive : mesh.primitives)
	{
		std::vector<float> positions{};
		std::vector<float> uvs{};
		std::vector<float> normals{};
		std::vector<float> colors{};
		std::vector<int> bonesIDs{};
		std::vector<float> weights{};
		std::vector<uint32_t> indices{};

		std::string accessorId;

		// ------------ Vertices ------------
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_POSITION, accessorId))
		{
			accessorPos = &document->accessors.Get(accessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, *accessorPos);
			positions.insert(positions.end(), data.begin(), data.end());
			
		}
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_TEXCOORD_0, accessorId))
		{
			accessorTex = &document->accessors.Get(accessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, *accessorTex);
			uvs.insert(uvs.end(), data.begin(), data.end());
		}
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_NORMAL, accessorId))
		{
			accessorNor = &document->accessors.Get(accessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, *accessorNor);
			normals.insert(normals.end(), data.begin(), data.end());
		}
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_COLOR_0, accessorId))
		{
			accessorCol = &document->accessors.Get(accessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, *accessorNor);
			colors.insert(colors.end(), data.begin(), data.end());
		}
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_JOINTS_0, accessorId))
		{
			accessorJoi = &document->accessors.Get(accessorId);
			switch (accessorJoi->componentType)
			{
			case glTF::COMPONENT_BYTE: {
				const auto data = resourceReader->ReadBinaryData<int8_t>(*document, *accessorJoi);
				bonesIDs.insert(bonesIDs.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_BYTE: {
				const auto data = resourceReader->ReadBinaryData<uint8_t>(*document, *accessorJoi);
				bonesIDs.insert(bonesIDs.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_SHORT: {
				const auto data = resourceReader->ReadBinaryData<int16_t>(*document, *accessorJoi);
				bonesIDs.insert(bonesIDs.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_SHORT: {
				const auto data = resourceReader->ReadBinaryData<uint16_t>(*document, *accessorJoi);
				bonesIDs.insert(bonesIDs.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_INT: {
				const auto data = resourceReader->ReadBinaryData<uint32_t>(*document, *accessorJoi);
				bonesIDs.insert(bonesIDs.end(), data.begin(), data.end());
				break;
			}
			default:
				throw glTF::GLTFException("Unsupported accessor ComponentType");
			}
		}
		if (primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_WEIGHTS_0, accessorId))
		{
			accessorWei = &document->accessors.Get(accessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, *accessorWei);
			weights.insert(weights.end(), data.begin(), data.end());
		}

		// ------------ Indices ------------
		if (primitive.indicesAccessorId != "")
		{
			accessorInd = &document->accessors.Get(primitive.indicesAccessorId);
			switch (accessorInd->componentType)
			{
			case glTF::COMPONENT_BYTE: {
				const auto data = resourceReader->ReadBinaryData<int8_t>(*document, *accessorInd);
				for (int i = 0; i < data.size(); i++)
					indices.push_back((uint32_t)data[i]);
				//indices.insert(indices.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_BYTE: {
				const auto data = resourceReader->ReadBinaryData<uint8_t>(*document, *accessorInd);
				for (int i = 0; i < data.size(); i++)
					indices.push_back((uint32_t)data[i]);
				//indices.insert(indices.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_SHORT: {
				const auto data = resourceReader->ReadBinaryData<int16_t>(*document, *accessorInd);
				for (int i = 0; i < data.size(); i++)
					indices.push_back((uint32_t)data[i]);
				//indices.insert(indices.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_SHORT: {
				const auto data = resourceReader->ReadBinaryData<uint16_t>(*document, *accessorInd);
				for (int i = 0; i < data.size(); i++)
					indices.push_back((uint32_t)data[i]);
				//indices.insert(indices.end(), data.begin(), data.end());
				break;
			}
			case glTF::COMPONENT_UNSIGNED_INT: {
				const auto data = resourceReader->ReadBinaryData<uint32_t>(*document, *accessorInd);
				for (int i = 0; i < data.size(); i++)
					indices.push_back((uint32_t)data[i]);
				//indices.insert(indices.end(), data.begin(), data.end());
				break;
			}
			default:
				throw glTF::GLTFException("Unsupported accessor ComponentType");
			}
		}

		Primitive myPrimitive;

		// ------------ Materials ------------
		const auto& material = document->materials.Get(primitive.materialId);

		// factors
		myPrimitive.pbrMaterial.alphaCutoff = material.alphaCutoff;
		myPrimitive.pbrMaterial.alphaMode = material.alphaMode;
		myPrimitive.pbrMaterial.baseColorFactor = vec4(&material.metallicRoughness.baseColorFactor.r);
		myPrimitive.pbrMaterial.doubleSided = material.doubleSided;
		myPrimitive.pbrMaterial.emissiveFactor = vec3(&material.emissiveFactor.r);
		myPrimitive.pbrMaterial.metallicFactor = material.metallicRoughness.metallicFactor;
		myPrimitive.pbrMaterial.roughnessFactor = material.metallicRoughness.roughnessFactor;

		// textures
		const auto baseColorImage = getImage(material.metallicRoughness.baseColorTexture.textureId);
		const auto metallicRoughnessImage = getImage(material.metallicRoughness.metallicRoughnessTexture.textureId);
		const auto normalImage = getImage(material.normalTexture.textureId);
		const auto occlusionImage = getImage(material.occlusionTexture.textureId);
		const auto emissiveImage = getImage(material.emissiveTexture.textureId);
		myPrimitive.loadTexture(TextureType::BaseColor, folderPath, baseColorImage, document, resourceReader);
		myPrimitive.loadTexture(TextureType::MetallicRoughness, folderPath, metallicRoughnessImage, document, resourceReader);
		myPrimitive.loadTexture(TextureType::Normal, folderPath, normalImage, document, resourceReader);
		myPrimitive.loadTexture(TextureType::Occlusion, folderPath, occlusionImage, document, resourceReader);
		myPrimitive.loadTexture(TextureType::Emissive, folderPath, emissiveImage, document, resourceReader);


		myPrimitive.vertexOffset = (uint32_t)myMesh->vertices.size();
		myPrimitive.verticesSize = (uint32_t)accessorPos->count;
		myPrimitive.indexOffset = (uint32_t)myMesh->indices.size();
		myPrimitive.indicesSize = (uint32_t)indices.size();
		myPrimitive.min = vec3(&accessorPos->min[0]);
		myPrimitive.max = vec3(&accessorPos->max[0]);
		myPrimitive.calculateBoundingSphere();
		myPrimitive.hasBones = bonesIDs.size() && weights.size();

		myMesh->primitives.push_back(myPrimitive);
		for (size_t i = 0; i < accessorPos->count; i++) {
			Vertex vertex;
			vertex.position = positions.size() > 0 ? vec3(&positions[i * 3]) : vec3();
			vertex.uv = uvs.size() > 0 ? vec2(&uvs[i * 2]) : vec2();
			vertex.normals = normals.size() > 0 ? vec3(&normals[i * 3]) : vec3();
			vertex.color = colors.size() > 0 ? vec4(&colors[i * 4]) : vec4();
			vertex.bonesIDs = bonesIDs.size() > 0 ? ivec4((int*)&bonesIDs[i * 4]) : ivec4();
			vertex.weights = weights.size() > 0 ? vec4(&weights[i * 4]) : vec4();
			myMesh->vertices.push_back(vertex);
		}
		for (size_t i = 0; i < indices.size(); i++) {
			myMesh->indices.push_back(indices[i]);
		}
	}
	node->mesh = myMesh;
}

void Model::loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show)
{
	// reads and gets the document and resourceReader objects
	readGltf(std::filesystem::path(folderPath + modelName));
	for (auto& node : document->GetDefaultScene().nodes)
	{
		loadNode(nullptr, document->nodes.Get(node), folderPath);
	}
	loadAnimations();
	loadSkins();

	for (auto node : linearNodes) {
		// Assign skins
		if (node->skinIndex > -1) {
			node->skin = skins[node->skinIndex];
		}
	}
}

void Model::loadModel(const std::string& folderPath, const std::string& modelName, bool show)
{
	// TODO: Copy the actual nodes and not the pointers
	for (auto& model : models) {
		if (model.fullPathName == folderPath + modelName) {
			*this = model;
			render = show;
			isCopy = true;
			animationTimer = 0.f;
			createUniformBuffers();
			createDescriptorSets();
			return;
		}
	}

	loadModelGltf(folderPath, modelName, show);

	name = modelName;
	fullPathName = folderPath + modelName;
	render = show;
	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorSets();
}

vk::DescriptorSetLayout Model::getDescriptorSetLayout()
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
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
	//ALPHA_OPAQUE
	for (auto& node : linearNodes) {
		if (node->mesh) {
			for (auto &primitive : node->mesh->primitives) {
				if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 1) {
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo.layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
					cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
				}
			}
		}
	}
	// ALPHA_MASK
	for (auto& node : linearNodes) {
		if (node->mesh) {
			for (auto &primitive : node->mesh->primitives) {
				// ALPHA CUT
				if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 2) {
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo.layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
					cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
				}
			}
		}
	}
	// ALPHA_BLEND
	for (auto& node : linearNodes) {
		if (node->mesh) {
			for (auto &primitive : node->mesh->primitives) {
				// ALPHA CUT
				if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 3) {
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo.layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
					cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
				}
			}
		}
	}
}

void frustumCheckAsync(mat4& modelMatrix, Mesh* mesh, Camera& camera, uint32_t index)
{
	vec4 bs = modelMatrix * mesh->ubo.matrix * vec4(vec3(mesh->primitives[index].boundingSphere), 1.0f);
	bs.w = mesh->primitives[index].boundingSphere.w * mesh->ubo.matrix[0][0]; // scale 
	mesh->primitives[index].cull = !camera.SphereInFrustum(bs);
}

void updateNodeAsync(mat4& modelMatrix, Node* node, Camera& camera)
{
	if (node->mesh) {
		node->update(camera);

		// async calls should be at least bigger than a number, else this will be slower
		if (node->mesh->primitives.size() > 3) {
			std::vector<std::future<void>> futures(node->mesh->primitives.size());
			for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
				futures[i] = std::async(std::launch::async, frustumCheckAsync, modelMatrix, node->mesh, camera, i);
			for (auto& f : futures)
				f.get();
		}
		else {
			for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
				frustumCheckAsync(modelMatrix, node->mesh, camera, i);
		}
	}
}

void Model::update(vm::Camera& camera, float delta)
{
	if (render) {
		ubo.previousMatrix = ubo.matrix;
		ubo.view = camera.view;
		ubo.projection = camera.projection;
		if (script) {
			script->update(delta);
			ubo.matrix = script->getValue<Transform>("transform").matrix() * transform;
		}
		else {
			ubo.matrix = transform;
		}
		memcpy(uniformBuffer.data, &ubo, sizeof(ubo));

		if (animations.size() > 0) {
			animationTimer += delta;
			if (animationTimer > animations[animationIndex].end) {
				animationTimer -= animations[animationIndex].end;
			}
			updateAnimation(animationIndex, animationTimer);
		}

		// async calls should be at least bigger than a number, else this will be slower
		if (linearNodes.size() > 3) {
			std::vector<std::future<void>> futureNodes(linearNodes.size());
			for (uint32_t i = 0; i < linearNodes.size(); i++)
				futureNodes[i] = std::async(std::launch::async, updateNodeAsync, ubo.matrix, linearNodes[i], camera);
			for (auto& f : futureNodes)
				f.get();
		}
		else {
			for (uint32_t i = 0; i < linearNodes.size(); i++)
				updateNodeAsync(ubo.matrix, linearNodes[i], camera);
		}
	}
}

void Model::updateAnimation(uint32_t index, float time)
{
	if (index > static_cast<uint32_t>(animations.size()) - 1) {
		std::cout << "No animation with index " << index << std::endl;
		return;
	}
	Animation &animation = animations[index];

	for (auto& channel : animation.channels) {
		vm::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size())
			continue;

		for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
				if (u <= 1.0f) {
					switch (channel.path) {
					case vm::AnimationChannel::PathType::TRANSLATION: {
						cvec4 t = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = vec3(t);
						break;
					}
					case vm::AnimationChannel::PathType::SCALE: {
						cvec4 s = mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = vec3(s);
						break;
					}
					case vm::AnimationChannel::PathType::ROTATION: {
						cquat q1(&sampler.outputsVec4[i].x);
						cquat q2(&sampler.outputsVec4[i + 1].x);
						channel.node->rotation = normalize(slerp(q1, q2, u));
						break;
					}
					}
				}
			}
		}
	}
}
// position x, y, z and radius w
vec4 Model::getBoundingSphere()
{
	for (auto& node : linearNodes) {
		if (node->mesh) {
			for (auto &primitive : node->mesh->primitives) {
				for (auto& vertex : node->mesh->vertices) {
					float temp = length(vertex.position);
					if (temp > boundingSphere.w)
						boundingSphere.w = temp;
				}
			}
		}
	}
	return boundingSphere; // unscaled bounding sphere with 0,0,0 origin
}

void Model::loadNode(vm::Node* parent, const Microsoft::glTF::Node& node, const std::string& folderPath)
{
	vm::Node *newNode = new vm::Node{};
	newNode->index = !node.id.empty() ? (uint32_t)document->nodes.GetIndex(node.id) : -1;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = !node.skinId.empty() ? (int32_t)document->skins.GetIndex(node.skinId) : -1;

	// Generate local node matrix
	if (!node.HasValidTransformType()) throw glTF::InvalidGLTFException("Node " + node.name + " has Invalid TransformType");
	newNode->transformationType = (TransformationType)node.GetTransformationType();
	newNode->translation = vec3(&node.translation.x);
	newNode->scale = vec3(&node.scale.x);
	newNode->rotation = quat(&node.rotation.x);
	newNode->matrix = mat4(&node.matrix.values[0]);

	// Node with children
	for (auto& child : node.children) {
		loadNode(newNode, document->nodes.Get(child), folderPath);
	}
	getMesh(newNode, node.meshId, folderPath);
	if (parent)
		parent->children.push_back(newNode);
	else
		nodes.push_back(newNode);
	linearNodes.push_back(newNode);
}

void vm::Model::loadAnimations()
{
	auto getNode = [](std::vector<Node*>& linearNodes, size_t index) -> Node* {
		for (auto& node : linearNodes) {
			if (node->index == index)
				return node;
		}
		return nullptr;
	};

	for (auto& anim : document->animations.Elements()) {
		vm::Animation animation{};
		animation.name = anim.name;
		if (anim.name.empty()) {
			animation.name = std::to_string(animations.size());
		}

		// Samplers
		for (auto &samp : anim.samplers.Elements()) {
			vm::AnimationSampler sampler{};
			if (samp.interpolation == glTF::INTERPOLATION_LINEAR) {
				sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
			}
			if (samp.interpolation == glTF::INTERPOLATION_STEP) {
				sampler.interpolation = AnimationSampler::InterpolationType::STEP;
			}
			if (samp.interpolation == glTF::INTERPOLATION_CUBICSPLINE) {
				sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
			}
			// Read sampler input time values
			{
				const glTF::Accessor &accessor = document->accessors.Get(samp.inputAccessorId);
				if (accessor.componentType != glTF::COMPONENT_FLOAT)
					throw std::runtime_error("Animation componentType is not equal to float");
				const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);
				sampler.inputs.insert(sampler.inputs.end(), data.begin(), data.end());

				for (auto input : sampler.inputs) {
					if (input < animation.start) {
						animation.start = input;
					};
					if (input > animation.end) {
						animation.end = input;
					}
				}
			}
			// Read sampler output T/R/S values 
			{
				const glTF::Accessor &accessor = document->accessors.Get(samp.outputAccessorId);
				if (accessor.componentType != glTF::COMPONENT_FLOAT)
					throw std::runtime_error("Animation componentType is not equal to float");
				const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);

				switch (accessor.type) {
				case glTF::AccessorType::TYPE_VEC3: {
					for (size_t i = 0; i < accessor.count; i++) {
						vec3 v3(&data[i * 3]);
						sampler.outputsVec4.push_back(vec4(v3, 0.0f));
					}
					break;
				}
				case glTF::AccessorType::TYPE_VEC4: {
					for (size_t i = 0; i < accessor.count; i++) {
						sampler.outputsVec4.push_back(vec4(&data[i * 4]));
					}
					break;
				}
				default: {
					throw glTF::GLTFException("unknown accessor type for TRS");
				}
				}
			}
			animation.samplers.push_back(sampler);
		}

		// Channels
		for (auto &source : anim.channels.Elements()) {
			vm::AnimationChannel channel{};

			if (source.target.path == glTF::TARGET_ROTATION) {
				channel.path = AnimationChannel::PathType::ROTATION;
			}
			if (source.target.path == glTF::TARGET_TRANSLATION) {
				channel.path = AnimationChannel::PathType::TRANSLATION;
			}
			if (source.target.path == glTF::TARGET_SCALE) {
				channel.path = AnimationChannel::PathType::SCALE;
			}
			if (source.target.path == glTF::TARGET_WEIGHTS) {
				std::cout << "weights not yet supported, skipping channel" << std::endl;
				continue;
			}
			channel.samplerIndex = (uint32_t)anim.samplers.GetIndex(source.samplerId);
			channel.node = getNode(linearNodes, document->nodes.GetIndex(source.target.nodeId));
			if (!channel.node) {
				continue;
			}
			animation.channels.push_back(channel);
		}
		animations.push_back(animation);
	}
}

void vm::Model::loadSkins()
{
	auto getNode = [](std::vector<Node*>& linearNodes, size_t index) -> Node* {
		for (auto& node : linearNodes) {
			if (node->index == index)
				return node;
		}
		return nullptr;
	};
	for (auto& source : document->skins.Elements()) {
		Skin *newSkin = new Skin();
		newSkin->name = source.name;

		// Find skeleton root node
		if (!source.skeletonId.empty()) {
			newSkin->skeletonRoot = getNode(linearNodes, document->nodes.GetIndex(source.skeletonId));
		}

		// Find joint nodes
		for (auto& jointID : source.jointIds) {
			Node* node = !jointID.empty() ? getNode(linearNodes, document->nodes.GetIndex(jointID)) : nullptr;
			if (node) {
				newSkin->joints.push_back(node);
			}
		}

		// Get inverse bind matrices
		if (!source.inverseBindMatricesAccessorId.empty()) {
			const glTF::Accessor &accessor = document->accessors.Get(source.inverseBindMatricesAccessorId);
			const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), data.data(), accessor.GetByteLength());
		}
		skins.push_back(newSkin);
	}
}

void Model::createVertexBuffer()
{
	std::vector<Vertex> vertices{};
	for (auto& node : linearNodes) {
		if (node->mesh) {
			node->mesh->vertexOffset = (uint32_t)vertices.size();
			for (auto& vertex : node->mesh->vertices) {
				vertices.push_back(vertex);
			}
		}
	}
	numberOfVertices = (uint32_t)vertices.size();
	vertexBuffer.createBuffer(sizeof(Vertex)*numberOfVertices, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(sizeof(Vertex)*numberOfVertices, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	staging.data = vulkan->device.mapMemory(staging.memory, 0, staging.size);
	memcpy(staging.data, vertices.data(), sizeof(Vertex)*numberOfVertices);
	vulkan->device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(staging.buffer, staging.size);
	staging.destroy();
}

void Model::createIndexBuffer()
{
	std::vector<uint32_t> indices{};
	for (auto& node : linearNodes) {
		if (node->mesh) {
			node->mesh->indexOffset = (uint32_t)indices.size();
			for (auto& index : node->mesh->indices) {
				indices.push_back(index);
			}
		}
	}
	numberOfIndices = (uint32_t)indices.size();
	indexBuffer.createBuffer(sizeof(uint32_t)*numberOfIndices, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(sizeof(uint32_t)*numberOfIndices, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	staging.data = vulkan->device.mapMemory(staging.memory, 0, staging.size);
	memcpy(staging.data, indices.data(), sizeof(uint32_t)*numberOfIndices);
	vulkan->device.unmapMemory(staging.memory);

	indexBuffer.copyBuffer(staging.buffer, staging.size);
	staging.destroy();
}

void Model::createUniformBuffers()
{
	uniformBuffer.createBuffer(4 * sizeof(mat4), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);
	if (!isCopy) {
		for (auto& node : linearNodes) {
			if (node->mesh) {
				node->mesh->createUniformBuffers();
			}
		}
	}
}

void Model::createDescriptorSets()
{
	auto const allocateInfo0 = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&Model::descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo0).at(0);

	auto const modelWriteSet = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)								// DescriptorSet dstSet;
		.setDstBinding(0)										// uint32_t dstBinding;
		.setDstArrayElement(0)									// uint32_t dstArrayElement;
		.setDescriptorCount(1)									// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)						// Buffer buffer;
			.setOffset(0)											// DeviceSize offset;
			.setRange(uniformBuffer.size));							// DeviceSize range;

	vulkan->device.updateDescriptorSets(modelWriteSet, nullptr);
	if (!isCopy) {
		for (auto& node : linearNodes) {
			if (!node->mesh) continue;
			auto const allocateInfo = vk::DescriptorSetAllocateInfo()
				.setDescriptorPool(vulkan->descriptorPool)
				.setDescriptorSetCount(1)
				.setPSetLayouts(&Mesh::descriptorSetLayout);
			node->mesh->descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

			auto const mvpWriteSet = vk::WriteDescriptorSet()
				.setDstSet(node->mesh->descriptorSet)					// DescriptorSet dstSet;
				.setDstBinding(0)										// uint32_t dstBinding;
				.setDstArrayElement(0)									// uint32_t dstArrayElement;
				.setDescriptorCount(1)									// uint32_t descriptorCount;
				.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
				.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
					.setBuffer(node->mesh->uniformBuffer.buffer)			// Buffer buffer;
					.setOffset(0)											// DeviceSize offset;
					.setRange(node->mesh->uniformBuffer.size));				// DeviceSize range;

			vulkan->device.updateDescriptorSets(mvpWriteSet, nullptr);
			for (auto& primitive : node->mesh->primitives) {

				Image& baseColor = primitive.pbrMaterial.baseColorTexture;
				Image& metallicRoughnes = primitive.pbrMaterial.metallicRoughnessTexture;
				Image& normals = primitive.pbrMaterial.normalTexture;
				Image& occlusion = primitive.pbrMaterial.occlusionTexture;
				Image& emissive = primitive.pbrMaterial.emissiveTexture;

				auto const allocateInfo2 = vk::DescriptorSetAllocateInfo()
					.setDescriptorPool(vulkan->descriptorPool)
					.setDescriptorSetCount(1)
					.setPSetLayouts(&primitive.descriptorSetLayout);
				primitive.descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo2).at(0);

				std::vector<vk::WriteDescriptorSet> textureWriteSets(6);
				textureWriteSets[0] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(0)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
					.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
						.setSampler(baseColor.sampler)									// Sampler sampler;
						.setImageView(baseColor.view)									// ImageView imageView;
						.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

				textureWriteSets[1] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(1)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
					.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
						.setSampler(metallicRoughnes.sampler)							// Sampler sampler;
						.setImageView(metallicRoughnes.view)							// ImageView imageView;
						.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

				textureWriteSets[2] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(2)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
					.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
						.setSampler(normals.sampler)									// Sampler sampler;
						.setImageView(normals.view)										// ImageView imageView;
						.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

				textureWriteSets[3] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(3)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
					.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
						.setSampler(occlusion.sampler)									// Sampler sampler;
						.setImageView(occlusion.view)									// ImageView imageView;
						.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

				textureWriteSets[4] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(4)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
					.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
						.setSampler(emissive.sampler)									// Sampler sampler;
						.setImageView(emissive.view)									// ImageView imageView;
						.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

				textureWriteSets[5] = vk::WriteDescriptorSet()
					.setDstSet(primitive.descriptorSet)								// DescriptorSet dstSet;
					.setDstBinding(5)												// uint32_t dstBinding;
					.setDstArrayElement(0)											// uint32_t dstArrayElement;
					.setDescriptorCount(1)											// uint32_t descriptorCount;
					.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
					.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
						.setBuffer(primitive.uniformBuffer.buffer)						// Buffer buffer;
						.setOffset(0)													// DeviceSize offset;
						.setRange(primitive.uniformBuffer.size));						// DeviceSize range;

				vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
			}
		}
	}
}

void Model::destroy()
{
	if (script) {
		delete script;
		script = nullptr;
	}
	uniformBuffer.destroy();
	if (!isCopy) {
		delete document;
		delete resourceReader;
		if (Model::descriptorSetLayout) {
			vulkan->device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
			Model::descriptorSetLayout = nullptr;
		}
		for (auto& node : linearNodes) {
			if (node->mesh) {
				node->mesh->destroy();
				delete node->mesh;
				node->mesh = nullptr;
			}
			delete node;
			node = nullptr;
		}
		for (auto& skin : skins) {
			delete skin;
			skin = nullptr;
		}
		for (auto& texture : Mesh::uniqueTextures)
			texture.second.destroy();
		Mesh::uniqueTextures.clear();
		vertexBuffer.destroy();
		indexBuffer.destroy();
	}
}