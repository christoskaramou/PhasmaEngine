#pragma once
#include "ECSBase.h"
#include <memory>
#include <map>

namespace vm
{
	class GameObject;

	class World
	{
	public:
		World() {}
		~World() {}

		template<class T, class... Params> T* AddSystem(Params&&... params);
		template<class T> T* GetSystem();
		template<class T> bool HasSystem();
		template<class T> void RemoveSystem();
		GameObject* CreateGameObject();
		GameObject* GetGameObject(size_t id);
		void RemoveGameObject(size_t id);
		void Update();

	private:
		std::map<size_t, std::unique_ptr<BaseSystem>> m_systems;
		std::map<size_t, std::unique_ptr<GameObject>> m_gameObjects;
	};
}