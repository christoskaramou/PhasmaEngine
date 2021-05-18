#include "EventSystem.h"

namespace pe
{
	void EventSystem::DispatchEvent(EventType type, const std::any& data)
	{
		if (m_events.find(type) != m_events.end())
			m_events[type].Invoke(data);
	}

	void EventSystem::SendEvent(EventType type, const std::any& data)
	{
		if (m_events.find(type) != m_events.end())
			m_processEvents.push_back(std::make_pair(&m_events[type], data));
	}

	void EventSystem::RegisterEvent(EventType type)
	{
		if (m_events.find(type) == m_events.end())
			m_events[type] = Delegate<std::any>();
	}

	void EventSystem::UnregisterEvent(EventType type)
	{
		if (m_events.find(type) != m_events.end())
			m_events.erase(type);
	}

	void EventSystem::RegisterEventAction(EventType type, Func&& func)
	{
		if (m_events.find(type) != m_events.end())
			m_events[type] += std::forward<Func>(func);
	}

	void EventSystem::UnregisterEventAction(EventType type, Func&& func)
	{
		if (m_events.find(type) != m_events.end())
			m_events[type] -= std::forward<Func>(func);
	}

	void EventSystem::ProcessEvents()
	{
		for (auto& delegate : m_processEvents)
			(*delegate.first).Invoke(delegate.second);

		m_processEvents.clear();
	}

	void EventSystem::ClearEvents()
	{
		m_events.clear();
		m_processEvents.clear();
	}
}
