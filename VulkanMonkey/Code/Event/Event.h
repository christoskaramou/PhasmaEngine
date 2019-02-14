#pragma once

#include <vector>
#include <map>

namespace vm {

#define SUBSCRIBE_TO_EVENT(eventType, func) EventSystem::get().subscribe(eventType, func)
#define FIRE_EVENT(eventType, pData) EventSystem::get().fire(eventType, pData)
#define UNSUBSCRIBE_FROM_EVENT(eventType, ID) EventSystem::get().unsubscribe(eventType, ID)

	constexpr auto BITSET(uint32_t x) { return 1 << x; }
	enum struct EventType
	{
		QUIT = BITSET(0),
		KEY_DOWN = BITSET(1),
		KEY_UP = BITSET(2),
		RANDOM1 = BITSET(3),
		RANDOM2 = BITSET(4),
		RANDOM3 = BITSET(5)
	};

	typedef void(*Func_t)(void*);
	struct Func {
		Func_t func;
		uint32_t ID;
	};

	struct EventSystem
	{
		static EventSystem& get();

		uint32_t subscribe(EventType eventType, Func_t func);
		void unsubscribe(EventType eventType, uint32_t ID);
		void fire(EventType eventType, void* pData = nullptr);

	private:
		std::map<EventType, std::vector<Func>> m_subscribers{};

		EventSystem() {};
		EventSystem(EventSystem const&) {};
		EventSystem& operator=(EventSystem const&) {};
		~EventSystem() {};
	};
}