#include "Model.h"
#include "Mesh.h"
#include "../Core/Queue.h"
#include <iostream>
#include <future>
#include <deque>
#include <GLTFSDK/GLBResourceReader.h>
#include <GLTFSDK/Deserialize.h>

namespace vm
{
	using namespace Microsoft;

	vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;
	std::vector<Model> Model::models{};
	Pipeline* Model::pipeline = nullptr;
	vk::CommandBuffer Model::commandBuffer = nullptr;

	bool endsWith(const std::string& mainStr, const std::string& toMatch)
	{
		if (mainStr.size() >= toMatch.size() &&
			mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
			return true;
		return false;
	}

	void Model::readGltf(const std::filesystem::path& file)
	{
		if (file.extension() != ".gltf" && file.extension() != ".glb")
			throw glTF::GLTFException("Model type not supported");

		std::string manifest;

		// Pass the absolute path, without the filename, to the stream reader
		auto streamReader = std::make_unique<StreamReader>(file.parent_path());
		const std::filesystem::path pathFile = file.filename();
		// Pass a UTF-8 encoded filename to GetInputString
		auto gltfStream = streamReader->GetInputStream(pathFile.string());
		if (file.extension() == ".gltf") {
			resourceReader = new glTF::GLTFResourceReader(std::move(streamReader));
			// Read the contents of the glTF file into a std::stringstream
			std::stringstream manifestStream;
			manifestStream << gltfStream->rdbuf();
			manifest = manifestStream.str();
		}
		else {
			// GLBResourceReader derives from GLTFResourceReader
			glTF::GLBResourceReader* resourceReaderGLB = new glTF::GLBResourceReader(std::move(streamReader), std::move(gltfStream));
			manifest = resourceReaderGLB->GetJson();
			resourceReader = static_cast<glTF::GLTFResourceReader*>(resourceReaderGLB);
		}

		//std::cout << manifest;

		try
		{
			document = new glTF::Document();
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

	glTF::Image* Model::getImage(const std::string& textureID) const
	{
		return textureID.empty() ? nullptr :
			const_cast<glTF::Image*>
			(&document->images.Get(document->textures.Get(textureID).imageId));
	}

	template <typename T>
	void Model::getVertexData(std::vector<T>& vec, const std::string& accessorName, const glTF::MeshPrimitive& primitive) const
	{
		std::string accessorId;
		if (primitive.TryGetAttributeAccessorId(accessorName, accessorId))
		{
			const auto& doc = *document;
			const auto& accessor = doc.accessors.Get(accessorId);

			switch (accessor.componentType)
			{
			case glTF::COMPONENT_FLOAT: {
				const auto data = resourceReader->ReadBinaryData<float>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](float value) -> T { return static_cast<T>(value); });
				break;
			}
			case glTF::COMPONENT_BYTE: {
				const auto data = resourceReader->ReadBinaryData<int8_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](int8_t value) -> T { return static_cast<T>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_BYTE: {
				const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint8_t value) -> T { return static_cast<T>(value); });
				break;
			}
			case glTF::COMPONENT_SHORT: {
				const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](int16_t value) -> T { return static_cast<T>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_SHORT: {
				const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint16_t value) -> T { return static_cast<T>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_INT: {
				const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint32_t value) -> T { return static_cast<T>(value); });
				break;
			}
			default:
				throw glTF::GLTFException("Unsupported accessor ComponentType");
			}
		}
	}

	void Model::getIndexData(std::vector<uint32_t>& vec, const glTF::MeshPrimitive& primitive) const
	{
		if (!primitive.indicesAccessorId.empty())
		{
			const auto& doc = *document;
			const auto& accessor = doc.accessors.Get(primitive.indicesAccessorId);

			switch (accessor.componentType)
			{
			case glTF::COMPONENT_BYTE: {
				const auto data = resourceReader->ReadBinaryData<int8_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](int8_t value) -> uint32_t { return static_cast<uint32_t>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_BYTE: {
				const auto data = resourceReader->ReadBinaryData<uint8_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint8_t value) -> uint32_t { return static_cast<uint32_t>(value); });
				break;
			}
			case glTF::COMPONENT_SHORT: {
				const auto data = resourceReader->ReadBinaryData<int16_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](int16_t value) -> uint32_t { return static_cast<uint32_t>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_SHORT: {
				const auto data = resourceReader->ReadBinaryData<uint16_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint16_t value) -> uint32_t { return static_cast<uint32_t>(value); });
				break;
			}
			case glTF::COMPONENT_UNSIGNED_INT: {
				const auto data = resourceReader->ReadBinaryData<uint32_t>(doc, accessor);
				vec.resize(data.size());
				std::transform(data.begin(), data.end(), vec.begin(), [](uint32_t value) -> uint32_t { return value; });
				break;
			}
			default:
				throw glTF::GLTFException("Unsupported accessor ComponentType");
			}
		}
	}

	void Model::getMesh(Pointer<vm::Node>& node, const std::string& meshID, const std::string& folderPath)
	{
		if (!node || meshID.empty()) return;
		const auto& mesh = document->meshes.Get(meshID);

		node->mesh = new Mesh();
		auto& myMesh = node->mesh;

		for (const auto& primitive : mesh.primitives)
		{
			std::vector<float> positions{};
			std::vector<float> uvs{};
			std::vector<float> normals{};
			std::vector<float> colors{};
			std::vector<int> bonesIDs{};
			std::vector<float> weights{};
			std::vector<uint32_t> indices{};

			// ------------ Vertices ------------
			getVertexData(positions, glTF::ACCESSOR_POSITION, primitive);
			getVertexData(uvs, glTF::ACCESSOR_TEXCOORD_0, primitive);
			getVertexData(normals, glTF::ACCESSOR_NORMAL, primitive);
			getVertexData(colors, glTF::ACCESSOR_COLOR_0, primitive);
			getVertexData(bonesIDs, glTF::ACCESSOR_JOINTS_0, primitive);
			getVertexData(weights, glTF::ACCESSOR_WEIGHTS_0, primitive);

			// ------------ Indices ------------
			getIndexData(indices, primitive);

			// ------------ Materials ------------
			const auto& material = document->materials.Get(primitive.materialId);

			myMesh->primitives.push_back({});
			auto& myPrimitive = myMesh->primitives.back();

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
			myPrimitive.loadTexture(MaterialType::BaseColor, folderPath, baseColorImage, document, resourceReader);
			myPrimitive.loadTexture(MaterialType::MetallicRoughness, folderPath, metallicRoughnessImage, document, resourceReader);
			myPrimitive.loadTexture(MaterialType::Normal, folderPath, normalImage, document, resourceReader);
			myPrimitive.loadTexture(MaterialType::Occlusion, folderPath, occlusionImage, document, resourceReader);
			myPrimitive.loadTexture(MaterialType::Emissive, folderPath, emissiveImage, document, resourceReader);


			std::string accessorId;
			primitive.TryGetAttributeAccessorId(glTF::ACCESSOR_POSITION, accessorId);
			const glTF::Accessor* accessorPos = &document->accessors.Get(accessorId);
			myPrimitive.vertexOffset = static_cast<uint32_t>(myMesh->vertices.size());
			myPrimitive.verticesSize = static_cast<uint32_t>(accessorPos->count);
			myPrimitive.indexOffset = static_cast<uint32_t>(myMesh->indices.size());
			myPrimitive.indicesSize = static_cast<uint32_t>(indices.size());
			myPrimitive.min = vec3(&accessorPos->min[0]);
			myPrimitive.max = vec3(&accessorPos->max[0]);
			myPrimitive.calculateBoundingSphere();
			myPrimitive.hasBones = !bonesIDs.empty() && !weights.empty();

			for (size_t i = 0; i < accessorPos->count; i++) {
				Vertex vertex;
				vertex.position = !positions.empty() ? vec3(&positions[i * 3]) : vec3();
				vertex.uv = !uvs.empty() ? vec2(&uvs[i * 2]) : vec2();
				vertex.normals = !normals.empty() ? vec3(&normals[i * 3]) : vec3();
				vertex.color = !colors.empty() ? vec4(&colors[i * 4]) : vec4();
				vertex.bonesIDs = !bonesIDs.empty() ? ivec4(&bonesIDs[i * 4]) : ivec4();
				vertex.weights = !weights.empty() ? vec4(&weights[i * 4]) : vec4();
				myMesh->vertices.push_back(vertex);
			}
			for (auto& index : indices) {
				myMesh->indices.push_back(index);
			}
		}
	}

	void Model::loadModelGltf(const std::string& folderPath, const std::string& modelName, bool show)
	{
		// reads and gets the document and resourceReader objects
		readGltf(std::filesystem::path(folderPath + modelName));

		for (auto& node : document->GetDefaultScene().nodes)
			loadNode({}, document->nodes.Get(node), folderPath);
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
		loadModelGltf(folderPath, modelName, show);
		//calculateBoundingSphere();
		name = modelName;
		fullPathName = folderPath + modelName;
		render = show;
		createVertexBuffer();
		createIndexBuffer();
		createUniformBuffers();
		createDescriptorSets();
	}

	vk::DescriptorSetLayout* Model::getDescriptorSetLayout()
	{
		if (!descriptorSetLayout) {

			vk::DescriptorSetLayoutBinding dslb;
			dslb.binding = 0;
			dslb.descriptorCount = 1; // number of descriptors contained
			dslb.descriptorType = vk::DescriptorType::eUniformBuffer;
			dslb.stageFlags = vk::ShaderStageFlagBits::eVertex;

			vk::DescriptorSetLayoutCreateInfo dslci;
			dslci.bindingCount = 1;
			dslci.pBindings = &dslb;
			descriptorSetLayout = VulkanContext::get()->device->createDescriptorSetLayout(dslci);
		}
		return &descriptorSetLayout;
	}

	void Model::updateAnimation(uint32_t index, float time)
	{
		if (index > static_cast<uint32_t>(animations.size()) - 1) {
			std::cout << "No animation with index " << index << std::endl;
			return;
		}
		Animation& animation = animations[index];

		for (auto& channel : animation.channels) {
			vm::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
			if (sampler.inputs.size() > sampler.outputsVec4.size())
				continue;

			for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
				if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
					const float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
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

	void frustumCheckAsync(Model& model, Pointer<Mesh>& mesh, Camera& camera, uint32_t index)
	{
		cmat4 trans = model.ubo.matrix * mesh->ubo.matrix;
		vec4 bs = trans * vec4(vec3(mesh->primitives[index].boundingSphere), 1.0f);
		bs.w = mesh->primitives[index].boundingSphere.w * abs(trans.scale().x); // scale 
		mesh->primitives[index].cull = !camera.SphereInFrustum(bs);
		mesh->primitives[index].transformedBS = bs;
	}

	void updateNodeAsync(Model& model, Pointer<Node>& node, Camera& camera)
	{
		if (node->mesh) {
			node->update(camera);

			// async calls should be at least bigger than a number, else this will be slower
			if (node->mesh->primitives.size() > 3) {
				std::vector<std::future<void>> futures(node->mesh->primitives.size());
				for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
					futures[i] = std::async(std::launch::async, frustumCheckAsync, model, node->mesh, camera, i);
				for (auto& f : futures)
					f.get();
			}
			else {
				for (uint32_t i = 0; i < node->mesh->primitives.size(); i++)
					frustumCheckAsync(model, node->mesh, camera, i);
			}
		}
	}

	void Model::update(vm::Camera& camera, double delta)
	{
		if (render) {
			ubo.previousMatrix = ubo.matrix;
			ubo.view = camera.view;
			ubo.previousView = camera.previousView;
			ubo.projection = camera.projection;
			if (script) {
				script->update(static_cast<float>(delta));
				ubo.matrix = script->getValue<Transform>("transform").matrix() * transform;
			}
			else {
				ubo.matrix = transform;
			}
			ubo.matrix = vm::transform(quat(radians(rot)), scale, pos) * ubo.matrix;
			Queue::memcpyRequest(&uniformBuffer, { { &ubo, sizeof(ubo), 0 } });
			//uniformBuffer.map();
			//memcpy(uniformBuffer.data, &ubo, sizeof(ubo));
			//uniformBuffer.flush();
			//uniformBuffer.unmap();

			if (!animations.empty()) {
				animationTimer += static_cast<float>(delta);
				if (animationTimer > animations[animationIndex].end) {
					animationTimer -= animations[animationIndex].end;
				}
				updateAnimation(animationIndex, animationTimer);
			}

			// async calls should be at least bigger than a number, else this will be slower
			if (linearNodes.size() > 3) {
				std::vector<std::future<void>> futureNodes(linearNodes.size());
				for (uint32_t i = 0; i < linearNodes.size(); i++)
					futureNodes[i] = std::async(std::launch::async, updateNodeAsync, *this, linearNodes[i], camera);
				for (auto& f : futureNodes)
					f.get();
			}
			else {
				for (auto& linearNode : linearNodes)
					updateNodeAsync(*this, linearNode, camera);
			}
		}
	}

	void Model::draw()
	{
		if (!render || !Model::pipeline)
			return;

		auto& cmd = Model::commandBuffer;
		const vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *Model::pipeline->pipeline);
		cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer.Value(), &offset);
		cmd.bindIndexBuffer(indexBuffer.buffer.Value(), 0, vk::IndexType::eUint32);

		//ALPHA_OPAQUE
		for (auto& node : linearNodes) {
			if (node->mesh) {
				for (auto& primitive : node->mesh->primitives) {
					if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 1) {
						cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo->layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
						cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
					}
				}
			}
		}
		// ALPHA_MASK
		for (auto& node : linearNodes) {
			if (node->mesh) {
				for (auto& primitive : node->mesh->primitives) {
					// ALPHA CUT
					if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 2) {
						cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo->layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
						cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
					}
				}
			}
		}
		// ALPHA_BLEND
		for (auto& node : linearNodes) {
			if (node->mesh) {
				for (auto& primitive : node->mesh->primitives) {
					// ALPHA CUT
					if (primitive.render && !primitive.cull && primitive.pbrMaterial.alphaMode == 3) {
						cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, Model::pipeline->pipeinfo->layout, 0, { node->mesh->descriptorSet, primitive.descriptorSet, descriptorSet }, nullptr);
						cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
					}
				}
			}
		}
	}

	// position x, y, z and radius w
	void Model::calculateBoundingSphere()
	{
		vec4 centerMax(0.f);
		vec4 centerMin(FLT_MAX);

		for (auto& node : linearNodes) {
			if (node->mesh) {
				for (auto& primitive : node->mesh->primitives) {
					cvec3 center = vec3(primitive.boundingSphere);

					const float lenMax = length(center) + primitive.boundingSphere.w;
					if (lenMax > centerMax.w)
						centerMax = vec4(center, lenMax);

					const float lenMin = lenMax - 2.f * primitive.boundingSphere.w;
					if (lenMin < centerMin.w)
						centerMin = vec4(center, lenMin);
				}
			}
		}
		const vec3 center = (vec3(centerMax) + vec3(centerMin)) * .5f;
		const float sphereRadius = length(vec3(centerMax) - center);
		boundingSphere = vec4(center, sphereRadius);
	}

	void Model::loadNode(Pointer<vm::Node> parent, const glTF::Node& node, const std::string& folderPath)
	{
		Pointer<vm::Node> newNode = new vm::Node{};
		newNode->index = !node.id.empty() ? static_cast<uint32_t>(document->nodes.GetIndex(node.id)) : -1;
		newNode->parent = parent;
		newNode->name = node.name;
		newNode->skinIndex = !node.skinId.empty() ? static_cast<int32_t>(document->skins.GetIndex(node.skinId)) : -1;

		// Generate local node matrix
		if (!node.HasValidTransformType()) throw glTF::InvalidGLTFException("Node " + node.name + " has Invalid TransformType");
		newNode->transformationType = static_cast<TransformationType>(node.GetTransformationType());
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
		//else
		//	nodes.push_back(newNode);
		linearNodes.push_back(newNode);
	}

	void vm::Model::loadAnimations()
	{
		const auto getNode = [](std::vector<Pointer<Node>>& linearNodes, size_t index) -> Pointer<Node> {
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
			for (auto& samp : anim.samplers.Elements()) {
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
					const glTF::Accessor& accessor = document->accessors.Get(samp.inputAccessorId);
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
					const glTF::Accessor& accessor = document->accessors.Get(samp.outputAccessorId);
					if (accessor.componentType != glTF::COMPONENT_FLOAT)
						throw std::runtime_error("Animation componentType is not equal to float");
					const auto data = resourceReader->ReadBinaryData<float>(*document, accessor);

					switch (accessor.type) {
					case glTF::AccessorType::TYPE_VEC3: {
						for (size_t i = 0; i < accessor.count; i++) {
							const vec3 v3(&data[i * 3]);
							sampler.outputsVec4.emplace_back(v3, 0.0f);
						}
						break;
					}
					case glTF::AccessorType::TYPE_VEC4: {
						for (size_t i = 0; i < accessor.count; i++) {
							sampler.outputsVec4.emplace_back(&data[i * 4]);
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
			for (auto& source : anim.channels.Elements()) {
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
				channel.samplerIndex = static_cast<uint32_t>(anim.samplers.GetIndex(source.samplerId));
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
		const auto getNode = [](std::vector<Pointer<Node>>& linearNodes, size_t index) -> Pointer<Node> {
			for (auto& node : linearNodes) {
				if (node->index == index)
					return node;
			}
			return nullptr;
		};
		for (auto& source : document->skins.Elements()) {
			Pointer<Skin> newSkin = new Skin{};
			newSkin->name = source.name;

			// Find skeleton root node
			if (!source.skeletonId.empty()) {
				newSkin->skeletonRoot = getNode(linearNodes, document->nodes.GetIndex(source.skeletonId));
			}

			// Find joint nodes
			for (auto& jointID : source.jointIds) {
				Pointer<Node> node = !jointID.empty() ? getNode(linearNodes, document->nodes.GetIndex(jointID)) : nullptr;
				if (node) {
					newSkin->joints.push_back(node);
				}
			}

			// Get inverse bind matrices
			if (!source.inverseBindMatricesAccessorId.empty()) {
				const glTF::Accessor& accessor = document->accessors.Get(source.inverseBindMatricesAccessorId);
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
				node->mesh->vertexOffset = static_cast<uint32_t>(vertices.size());
				for (auto& vertex : node->mesh->vertices) {
					vertices.push_back(vertex);
				}
			}
		}
		numberOfVertices = static_cast<uint32_t>(vertices.size());
		auto size = sizeof(Vertex) * numberOfVertices;
		vertexBuffer.createBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

		// Staging buffer
		Buffer staging;
		staging.createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		staging.map();
		staging.copyData(vertices.data());
		staging.flush();
		staging.unmap();

		vertexBuffer.copyBuffer(staging.buffer.Value(), staging.size.Value());
		staging.destroy();
	}

	void Model::createIndexBuffer()
	{
		std::vector<uint32_t> indices{};
		for (auto& node : linearNodes) {
			if (node->mesh) {
				node->mesh->indexOffset = static_cast<uint32_t>(indices.size());
				for (auto& index : node->mesh->indices) {
					indices.push_back(index);
				}
			}
		}
		numberOfIndices = static_cast<uint32_t>(indices.size());
		auto size = sizeof(uint32_t) * numberOfIndices;
		indexBuffer.createBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

		// Staging buffer
		Buffer staging;
		staging.createBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		staging.map();
		staging.copyData(indices.data());
		staging.flush();
		staging.unmap();

		indexBuffer.copyBuffer(staging.buffer.Value(), staging.size.Value());
		staging.destroy();
	}

	void Model::createUniformBuffers()
	{
		uniformBuffer.createBuffer(sizeof(ubo), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		uniformBuffer.map();
		uniformBuffer.zero();
		uniformBuffer.flush();
		uniformBuffer.unmap();
		for (auto& node : linearNodes) {
			if (node->mesh) {
				node->mesh->createUniformBuffers();
			}
		}
	}

	void Model::createDescriptorSets()
	{
		std::deque<vk::DescriptorImageInfo> dsii{};
		auto const wSetImage = [&dsii](vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image) {
			dsii.emplace_back(image.sampler.Value(), image.view.Value(), vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr };
		};
		std::deque<vk::DescriptorBufferInfo> dsbi{};
		auto const wSetBuffer = [&dsbi](vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer) {
			dsbi.emplace_back(buffer.buffer.Value(), 0, buffer.size.Value());
			return vk::WriteDescriptorSet{ dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr };
		};

		// model dSet
		vk::DescriptorSetAllocateInfo allocateInfo0;
		allocateInfo0.descriptorPool = VulkanContext::get()->descriptorPool.Value();
		allocateInfo0.descriptorSetCount = 1;
		allocateInfo0.pSetLayouts = getDescriptorSetLayout();
		descriptorSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo0).at(0);

		VulkanContext::get()->device->updateDescriptorSets(wSetBuffer(descriptorSet, 0, uniformBuffer), nullptr);

		// mesh dSets
		for (auto& node : linearNodes) {

			if (!node->mesh) continue;
			auto& mesh = node->mesh;

			vk::DescriptorSetAllocateInfo allocateInfo;
			allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool.Value();
			allocateInfo.descriptorSetCount = 1;
			allocateInfo.pSetLayouts = Mesh::getDescriptorSetLayout();
			mesh->descriptorSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo).at(0);

			VulkanContext::get()->device->updateDescriptorSets(wSetBuffer(mesh->descriptorSet, 0, mesh->uniformBuffer), nullptr);

			// primitive dSets
			for (auto& primitive : mesh->primitives) {

				vk::DescriptorSetAllocateInfo allocateInfo2;
				allocateInfo2.descriptorPool = VulkanContext::get()->descriptorPool.Value();
				allocateInfo2.descriptorSetCount = 1;
				allocateInfo2.pSetLayouts = Primitive::getDescriptorSetLayout();
				primitive.descriptorSet = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo2).at(0);

				std::vector<vk::WriteDescriptorSet> textureWriteSets{
					wSetImage(primitive.descriptorSet, 0, primitive.pbrMaterial.baseColorTexture),
					wSetImage(primitive.descriptorSet, 1, primitive.pbrMaterial.metallicRoughnessTexture),
					wSetImage(primitive.descriptorSet, 2, primitive.pbrMaterial.normalTexture),
					wSetImage(primitive.descriptorSet, 3, primitive.pbrMaterial.occlusionTexture),
					wSetImage(primitive.descriptorSet, 4, primitive.pbrMaterial.emissiveTexture),
					wSetBuffer(primitive.descriptorSet, 5, primitive.uniformBuffer)
				};
				VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
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
		delete document;
		delete resourceReader;
		if (Model::descriptorSetLayout) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Model::descriptorSetLayout);
			Model::descriptorSetLayout = nullptr;
		}
		for (auto& node : linearNodes) {
			if (node->mesh) {
				node->mesh->destroy();
				delete node->mesh.get();
				node->mesh = {};
			}
			delete node.get();
			node = {};
		}
		for (auto& skin : skins) {
			delete skin.get();
			skin = {};
		}
		//for (auto& texture : Mesh::uniqueTextures)
		//	texture.second.destroy();
		//Mesh::uniqueTextures.clear();
		vertexBuffer.destroy();
		indexBuffer.destroy();
	}
}