#pragma once

#include "../Image/Image.h"
#include "../Math/Math.h"

namespace vm {
	struct PBRMaterial
	{
		vec4 baseColorFactor;
		float metallicFactor{};
		float roughnessFactor{};
		vec3 emissiveFactor;
		float alphaCutoff{};
		bool doubleSided{};
		uint16_t alphaMode{};

		Image baseColorTexture;
		Image metallicRoughnessTexture;
		Image normalTexture;
		Image occlusionTexture;
		Image emissiveTexture;
	};
}