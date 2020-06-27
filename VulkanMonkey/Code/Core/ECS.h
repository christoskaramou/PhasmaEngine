#pragma once
#include <vector>
#include <memory>
#include <map>
#include <utility>

namespace vm
{
	class World;
	class GameObject;

	class BaseBehaviour
	{
	public:
		virtual ~BaseBehaviour() {}

	protected:
		virtual void Awake() {}
		virtual void Start() {}
		virtual void Update() {}
		virtual void FixedUpdate() {}
		virtual void LateUpdate() {}
		virtual void OnGUI() {}
		virtual void OnEnable() {}
		virtual void OnDisable() {}
	};

	class BaseObject
	{
	public:
		virtual ~BaseObject() {}

		static size_t nextID();
		static size_t nextTypeID();
		Transform& GetTransform() { return m_transform; }
		void SetTransform(const Transform& transform) { m_transform = transform; }
		Transform* GetParent() { return m_parent; }
		void SetParent(Transform* parent) { m_parent = parent; }
		bool IsEnabled() { return m_enable; }
		void Enable() { m_enable = true; }
		void Disable() { m_enable = false; }

	protected:
		Transform m_transform;
		Transform* m_parent;
		bool m_enable;
	};

	size_t BaseObject::nextID() { static size_t ID = 0; return ID++; };
	size_t BaseObject::nextTypeID() { static size_t typeID = 0; return typeID++; };

	class BaseComponent : public BaseObject, public BaseBehaviour
	{
	public:
		virtual ~BaseComponent() {}
	};

	template<class T>
	class Component : public BaseComponent
	{
	public:
		virtual ~Component() {}
		size_t GetID() { return m_id; }
		static size_t GetTypeID();
		GameObject* GetGameObject() { return m_gameObject; }
		void SetGameObject(GameObject* gameObject) { m_gameObject = gameObject; }

	protected:
		friend class GameObject;
		Component() : m_id(BaseObject::nextID()), m_gameObject(nullptr) { m_parent = nullptr; m_enable = true; }
		Component(GameObject* gameObject, Transform* parent = nullptr, bool enable = true) : m_id(BaseObject::nextID()), m_gameObject(gameObject) { m_parent = parent; m_enable = enable; }

	private:
		GameObject* m_gameObject;
		const size_t m_id;
		static const size_t s_typeID;
	};

	template<class T>
	const size_t Component<T>::s_typeID = BaseObject::nextTypeID();

	template<class T>
	size_t Component<T>::GetTypeID() { return Component<T>::s_typeID; }

	class GameObject : public BaseObject
	{
	public:
		~GameObject() {}
		size_t GetID() { return m_id; }
		World* GetWorld() { return m_world; }
		void SetWorld(World* world) { m_world = world; }

		template<class T>
		bool HasComponent()
		{
			if constexpr (std::is_base_of<Component<T>, T>::value)
			{
				if (m_components.count(T::GetTypeID()))
					return true;
				else
					return false;
			}
			else
			{
				throw std::runtime_error("GameObject::HasComponent: Type is not a Component");
			}
		}
		
		template<class T>
		T* GetComponent()
		{
			if constexpr (std::is_base_of<Component<T>, T>::value)
			{
				if (m_components.count(T::GetTypeID()))
					return static_cast<T*>(m_components[T::GetTypeID()].get());
				else
					return nullptr;
			}
			else
			{
				throw std::runtime_error("GameObject::GetComponent: Type is not a Component");
			}
		}
		
		template<class T, class... Params>
		T* AddComponent(Params&&... params)
		{
			if constexpr (std::is_base_of<Component<T>, T>::value)
			{
				if (!m_components.count(T::GetTypeID()))
				{
					T* pT = new T(std::forward<Params>(params)...);
					pT->SetGameObject(this);
					pT->SetParent(&m_transform);

					m_components[T::GetTypeID()] = std::unique_ptr<BaseComponent>(pT);

					return static_cast<T*>(m_components[T::GetTypeID()].get());
				}
				else
				{
					return nullptr;
				}
			}
			else
			{
				throw std::runtime_error("GameObject::AddComponent: Type is not a Component");
			}
		}

		template<class T>
		void RemoveComponent()
		{
			if constexpr (std::is_base_of<Component<T>, T>::value)
			{
				if (m_components.count(T::GetTypeID()))
					m_components.erase(T::GetTypeID());
			}
			else
			{
				throw std::runtime_error("GameObject::RemoveComponent: Type is not a Component");
			}
		}

	private:
		friend class World;
		GameObject() : m_world(nullptr), m_id(BaseObject::nextID()) { m_parent = nullptr;  m_enable = true; }

		const size_t m_id;
		World* m_world;
		std::map<size_t, std::unique_ptr<BaseComponent>> m_components;
	};

	class BaseSystem : public BaseObject, public BaseBehaviour
	{
	public:
		virtual ~BaseSystem() {}
	};

	template<class T>
	class System : public BaseSystem
	{
	public:
		System() : m_world(nullptr), m_id(BaseComponent::nextID()) { m_parent = nullptr;  m_enable = true; }
		virtual ~System() {}
		void SetWorld(World* world) { m_world = world; }
		size_t GetID() { return m_id; }
		static size_t GetTypeID();

		template<class U>
		void AddComponent(U* component)
		{
			if constexpr (std::is_base_of<Component<U>, U>::value)
			{
				if (!m_components.count(component->GetID()))
					m_components[component->GetID()] = static_cast<BaseComponent*>(component);
			}
			else
			{
				throw std::runtime_error("System::AddComponent: Type is not a Component");
			}
		}

	private:
		World* m_world;
		std::map<size_t, BaseComponent*> m_components;
		const size_t m_id;
		static const size_t s_typeID;
	};

	template<class T>
	const size_t System<T>::s_typeID = BaseObject::nextTypeID();

	template<class T>
	size_t System<T>::GetTypeID() { return System<T>::s_typeID; }

	class World
	{
	public:
		World() {}
		~World() {}

		template<class T, class... Params>
		T* AddSystem(Params&&... params)
		{
			if constexpr (std::is_base_of<System<T>, T>::value)
			{
				if (!m_systems.count(T::GetTypeID()))
				{
					m_systems[T::GetTypeID()] = std::make_unique<T>(std::forward<Params>(params)...);

					T* pT = static_cast<T*>(m_systems[T::GetTypeID()].get());
					pT->SetWorld(this);

					return pT;
				}
				else
				{
					return nullptr;
				}
			}
			else
			{
				throw std::runtime_error("World::AddSystem: Type is not a System");
			}
		}
		
		template<class T>
		T* GetSystem()
		{
			if constexpr (std::is_base_of<System<T>, T>::value)
			{
				if (m_systems.count(T::GetTypeID()))
					return static_cast<T*>(m_systems[T::GetTypeID()].get());
				else
					return nullptr;
			}
			else
			{
				throw std::runtime_error("World::GetSystem: Type is not a System");
			}
		}
		
		template<class T>
		bool HasSystem()
		{
			if constexpr (std::is_base_of<System<T>, T>::value)
			{
				if (m_systems.count(T::GetTypeID()))
					return true;
				else
					return false;
			}
			else
			{
				throw std::runtime_error("World::HasSystem: Type is not a System");
			}
		}

		template<class T>
		void RemoveSystem()
		{
			if constexpr (std::is_base_of<System<T>, T>::value)
			{
				if (m_systems.count(T::GetTypeID()))
					m_systems.erase(T::GetTypeID());
			}
			else
			{
				throw std::runtime_error("World::RemoveSystem: Type is not a System");
			}
		}

		GameObject* CreateGameObject()
		{
			GameObject* gameObject = new GameObject();
			gameObject->SetWorld(this);

			size_t key = gameObject->GetID();
			m_gameObjects[key] = std::unique_ptr<GameObject>(gameObject);

			return m_gameObjects[key].get();
		}

		GameObject* GetGameObject(size_t id)
		{
			if (m_gameObjects.count(id))
				return m_gameObjects[id].get();

			return nullptr;
		}

		void RemoveGameObject(size_t id)
		{
			if (m_gameObjects.count(id))
				m_gameObjects.erase(id);
		}

		void Update()
		{
			for (auto& system : m_systems)
			{
				//if (system.second->IsEnabled())
				//	system.second->Update();
			}
		}

	private:
		std::map<size_t, std::unique_ptr<BaseSystem>> m_systems;
		std::map<size_t, std::unique_ptr<GameObject>> m_gameObjects;
	};
}
