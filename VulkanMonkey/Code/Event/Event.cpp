#include "Event.h"
#include <algorithm>
using namespace vm;

Event Event::FrameStart = CREATE_EVENT();
Event Event::FrameEnd = CREATE_EVENT();
Event Event::Tick = CREATE_EVENT();
Event Event::OnExit = CREATE_EVENT();
Event Event::OnUpdate = CREATE_EVENT();
Event Event::OnRender = CREATE_EVENT();

EventSystem& EventSystem::get() {
	static EventSystem instance;
	return instance;
}

// returns the eventID
Event vm::EventSystem::createEvent()
{
	m_subscribers.push_back({});
	return { (uint32_t)m_subscribers.size() - 1, false };
}

// if func ID is needed, the return of subscribe should be stored
FuncID EventSystem::subscribe(Event event, Func_t&& func, uint32_t priority)
{
	static FuncID funcID = 0;
	if (m_subscribers.size() <= event.ID) exit(-30);
	auto& v = m_subscribers[event.ID];
	v.push_back({ std::forward<Func_t>(func), funcID, priority });
	std::sort(v.begin(), v.end(), [](Func& f1, Func& f2) {return f1.priority < f2.priority; });
	return funcID++;
}

// funcID is the stored value of the subscribe function return
void EventSystem::unsubscribe(Event event, FuncID funcID)
{
	if (m_subscribers.size() <= event.ID) exit(-30);
	auto& v = m_subscribers[event.ID];
	for (auto& it = v.begin(); it != v.end(); ++it)
		if (it->ID == funcID) {
			v.erase(it);
			v.shrink_to_fit();
			return;
		}
}

void EventSystem::fire(Event event, std::any data)
{
	if (event.handled || m_subscribers.size() <= event.ID)
		return;
	for (auto& sub : m_subscribers[event.ID])
		sub.func(data);
}