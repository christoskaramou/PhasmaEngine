#pragma once

#include "Camera.h"
#include "Compute.h"
#include "Deferred.h"
#include "Forward.h"
#include "Light.h"
#include "Mesh.h"
#include "Model.h"
#include "Shadows.h"
#include "Skybox.h"
#include "SSAO.h"
#include "SSR.h"
#include "Terrain.h"

namespace vm {
	struct Component {
		Component() = default;
		Component(Camera* camera);
		Component(Compute* compute);
		Component(Deferred* deferred);
		Component(Forward* forward);
		Component(Light* light);
		Component(Mesh* mesh);
		Component(Model* model);
		Component(Shadows* shadows);
		Component(SkyBox* skybox);
		Component(SSAO* ssao);
		Component(SSR* ssr);
		Component(Terrain* terrain);


	private:
		union
		{
			Camera* camera;
			Compute* compute;
			Deferred* deferred;
			Forward* forward;
			Light* light;
			Mesh* mesh;
			Model* model;
			Shadows* shadows;
			SkyBox* skybox;
			SSAO* ssao;
			SSR* ssr;
			Terrain* terrain;
		} active;
	};
}