#pragma once
#include <unordered_set>

namespace vm
{
	class Context;

	class ISystem
	{
	public:
		ISystem() : m_context(nullptr), m_enabled(false) {}
		virtual ~ISystem() {}

		virtual void Init() = 0;
		virtual void Update(double delta) = 0;
		virtual void Destroy() = 0;
		void AddComponentType(size_t type) { m_componentTypes.insert(type); }
		void RemoveComponentType(size_t type) { m_componentTypes.erase(type); }
		std::unordered_set<size_t>& GetComponentTypes() { return m_componentTypes; }
		void SetContext(Context* context) { m_context = context; }
		Context* GetContext() { return m_context; }
		bool IsEnabled() { return m_enabled; }
		void SetEnabled(bool enabled) { m_enabled = enabled; }

	private:
		Context* m_context;
		std::unordered_set<size_t> m_componentTypes;
		bool m_enabled;
	};
}
