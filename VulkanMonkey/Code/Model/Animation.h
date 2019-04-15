#pragma once

#include "../Math/Math.h"
#include <vector>
#include <limits>
#include <string>
#include "../Node/Node.h"

namespace vm {

	struct AnimationChannel {
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType path;
		Pointer<Node> node;
		int32_t samplerIndex;
	};

	struct AnimationSampler {
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType interpolation;
		std::vector<float> inputs;
		std::vector<vec4> outputsVec4;
	};

	struct Animation {
		std::string name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float start = std::numeric_limits<float>::max();
		float end = std::numeric_limits<float>::min();
	};
}