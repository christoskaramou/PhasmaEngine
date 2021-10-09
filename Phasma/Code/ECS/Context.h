/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Entity.h"
#include "System.h"
#include "Core/Base.h"
#include <vector>

#define CONTEXT Context::Get()
#define WORLD_ENTITY CONTEXT->WorldEntity

namespace pe
{
	class VulkanContext;
	
	class Context final
	{
	public:
		static auto Get()
		{
			static auto instance = new Context();
			return instance;
		}

		Context(Context const&) = delete;				// copy constructor

		Context(Context&&) noexcept = delete;			// move constructor

		Context& operator=(Context const&) = delete;	// copy assignment

		Context& operator=(Context&&) = delete;			// move assignment

		~Context() = default;							// destructor

	private:
		Context() = default;

	public:
		static Entity* WorldEntity;

		void InitSystems();

		void DestroySystems();
		
		void UpdateSystems(double delta);

		void DrawSystems();
		
		template<class T, class... Params>
		inline T* CreateSystem(Params&& ... params);
		
		template<class T>
		inline T* GetSystem();
		
		template<class T>
		inline bool HasSystem();
		
		template<class T>
		inline void RemoveSystem();
		
		Entity* CreateEntity();
		
		Entity* GetEntity(size_t id);
		
		void RemoveEntity(size_t id);
		
		VulkanContext* GetVKContext();
	
	private:
		std::unordered_map<size_t, SPtr<ISystem>> m_systems;
		std::unordered_map<size_t, IDrawSystem*> m_drawSystems; // Keep the pointers for draw systems. All are stored in m_systems
		std::unordered_map<size_t, SPtr<Entity>> m_entities;
	};
	
	template<class T>
	inline bool Context::HasSystem()
	{
		ValidateBaseClass<ISystem, T>();
		
		if (m_systems.find(GetTypeID<T>()) != m_systems.end())
			return true;
		else
			return false;
	}
	
	template<class T, class... Params>
	inline T* Context::CreateSystem(Params&& ... params)
	{
		if (!HasSystem<T>())
		{
			size_t id = GetTypeID<T>();
			m_systems[id] = std::make_shared<T>(std::forward<Params>(params)...);
			m_systems[id]->SetEnabled(true);

			if (std::is_base_of<IDrawSystem, T>::value)
				m_drawSystems[id] = static_cast<IDrawSystem*>(m_systems[id].get());
			
			return static_cast<T*>(m_systems[id].get());
		}
		else
		{
			return nullptr;
		}
	}
	
	template<class T>
	inline T* Context::GetSystem()
	{
		if (HasSystem<T>())
			return static_cast<T*>(m_systems[GetTypeID<T>()].get());
		else
			return nullptr;
	}
	
	template<class T>
	inline void Context::RemoveSystem()
	{
		if (HasSystem<T>())
			m_systems.erase(GetTypeID<T>());
	}
}
