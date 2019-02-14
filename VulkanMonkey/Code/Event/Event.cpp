#include "Event.h"

using namespace vm;

EventSystem& EventSystem::get() {
	static EventSystem instance;
	return instance;
}

uint32_t EventSystem::subscribe(EventType eventType, Func_t func)
{
	static uint32_t ID = 0;;
	m_subscribers[eventType].push_back({ func, ID });
	return ID++;
}

void EventSystem::unsubscribe(EventType eventType, uint32_t funcID)
{
	auto& v = m_subscribers[eventType];
	for (auto& it = v.begin(); it != v.end(); ++it)
		if (it->ID == funcID) {
			v.erase(it);
			v.shrink_to_fit();
			return;
		}
}

void EventSystem::fire(EventType eventType, void* pData)
{
	if (m_subscribers.find(eventType) == m_subscribers.end())
		return;

	for (auto& sub : m_subscribers[eventType])
	{
		sub.func(pData);
	}
}