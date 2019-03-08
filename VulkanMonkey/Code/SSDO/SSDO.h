#pragma once

#include "../SSAO/SSAO.h"

namespace vm {
	struct SSDO : SSAO
	{
		void update(Camera& camera);
		void createUniforms(std::map<std::string, Image>& renderTargets);
		void updateDescriptorSets(std::map<std::string, Image>& renderTargets);
	};

}