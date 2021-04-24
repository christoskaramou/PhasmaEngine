#pragma once

#include "../Core/Buffer.h"
#include "../Renderer/Pipeline.h"
#include "../Core/Image.h"
#include "../Core/Math.h"
#include "../Camera/Camera.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Framebuffer.h"
#include <vector>
#include <map>

namespace vk
{
	class DescriptorSet;
	class CommandBuffer;
}

namespace vm
{
	class TAA
	{
	public:
		TAA();

		~TAA();

		std::vector<Framebuffer> framebuffers {}, framebuffersSharpen {};
		Pipeline pipeline, pipelineSharpen;
		RenderPass renderPass, renderPassSharpen;
		Ref<vk::DescriptorSet> DSet, DSetSharpen;
		Image previous;
		Image frameImage;

		struct UBO
		{
			vec4 values;
			vec4 sharpenValues;
			mat4 invProj;
		} ubo;
		Buffer uniform;

		void Init();

		void update(const Camera& camera);

		void createUniforms(std::map<std::string, Image>& renderTargets);

		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);

		void draw(vk::CommandBuffer cmd, uint32_t imageIndex, std::map<std::string, Image>& renderTargets);

		void createRenderPasses(std::map<std::string, Image>& renderTargets);

		void createFrameBuffers(std::map<std::string, Image>& renderTargets);

		void createPipeline(std::map<std::string, Image>& renderTargets);

		void createPipelineSharpen(std::map<std::string, Image>& renderTargets);

		void createPipelines(std::map<std::string, Image>& renderTargets);

		void saveImage(const vk::CommandBuffer& cmd, Image& source) const;

		void destroy();
	};
}
