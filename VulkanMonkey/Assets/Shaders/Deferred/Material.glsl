#ifndef MATERIAL_H_
#define MATERIAL_H_

struct Material {
	vec3 albedo;
	float roughness;
	vec3 F0;
	float metallic;
};

#endif