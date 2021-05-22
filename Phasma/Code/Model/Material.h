#pragma once

#include "../Core/Image.h"
#include "../Core/Math.h"

namespace pe
{
	enum MaterialType
	{
		BaseColor,
		MetallicRoughness,
		Normal,
		Occlusion,
		Emissive
	};
	
	struct PBRMaterial
	{
		vec4 baseColorFactor;
		float metallicFactor {};
		float roughnessFactor {};
		vec3 emissiveFactor;
		float alphaCutoff {};
		bool doubleSided {};
		uint16_t alphaMode {};
		
		Image baseColorTexture;
		Image metallicRoughnessTexture;
		Image normalTexture;
		Image occlusionTexture;
		Image emissiveTexture;
	};
}