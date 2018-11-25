#include "../include/Component.h"

using namespace vm;

Component::Component(Camera * camera)
{
	active.camera = camera;
}

Component::Component(Compute * compute)
{
	active.compute = compute;
}

Component::Component(Deferred * deferred)
{
	active.deferred = deferred;
}

Component::Component(Forward * forward)
{
	active.forward = forward;
}

Component::Component(Light * light)
{
	active.light = light;
}

Component::Component(Mesh * mesh)
{
	active.mesh = mesh;
}

Component::Component(Model * model)
{
	active.model = model;
}

Component::Component(Shadows * shadows)
{
	active.shadows = shadows;
}

Component::Component(SkyBox * skybox)
{
	active.skybox = skybox;
}

Component::Component(SSAO * ssao)
{
	active.ssao = ssao;
}

Component::Component(SSR * ssr)
{
	active.ssr = ssr;
}

Component::Component(Terrain * terrain)
{
	active.terrain = terrain;
}
