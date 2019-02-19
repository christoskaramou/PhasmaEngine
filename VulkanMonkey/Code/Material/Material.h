#pragma once

#include "../Image/Image.h"
#include "../Math/Math.h"

namespace vm {
	// http://assimp.sourceforge.net/lib_html/material_8h.html#a7dd415ff703a2cc53d1c22ddbbd7dde0
	struct Material {
		// values
		std::string name = "";
		vec3 colorDiffuse = vec3(0.0f);
		vec3 colorSpecular = vec3(0.0f);
		vec3 colorAmbient = vec3(0.0f);
		vec3 colorEmissive = vec3(0.0f);
		vec3 colorTransparent = vec3(0.0f);
		int wireframe = 0;
		bool twoSided = 0;
		int shadingModel = 4;
		int blendFunc = 0;
		float opacity = 1.0f;
		float shininess = 0.0f;
		float shininessStrength = 1.0f;
		float refraction = 1.0f;

		// textures
		Image textureDiffuse;
		Image textureSpecular;
		Image textureAmbient;
		Image textureEmissive;
		Image textureHeight;
		Image textureNormals;
		Image textureShininess;
		Image textureOpacity;
		Image textureDisplacement;
		Image textureLight;
		Image textureReflection;
	};

	struct PBRMaterial
	{
		vec4 baseColorFactor;
		float metallicFactor;
		float roughnessFactor;
		vec3 emissiveFactor;
		std::string alphaMode;
		float alphaCutoff;
		bool doubleSided;

		Image baseColorTexture;
		Image metallicRoughnessTexture;
		Image normalTexture;
		Image occlusionTexture;
		Image emissiveTexture;
	};
}