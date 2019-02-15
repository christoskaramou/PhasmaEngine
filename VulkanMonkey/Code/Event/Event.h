#pragma once

#include <vector>
#include <functional>
#include <any>

namespace vm {

#define CREATE_EVENT EventSystem::get().createEvent
#define SUBSCRIBE_TO_EVENT EventSystem::get().subscribe
#define FIRE_EVENT EventSystem::get().fire
#define UNSUBSCRIBE_FROM_EVENT EventSystem::get().unsubscribe
	struct Event
	{
		uint32_t ID;
		bool handled;

		static Event FrameStart;
		static Event FrameEnd;
		static Event Tick;
		static Event OnExit;
		static Event OnUpdate;
		static Event OnRender;
	};

	typedef uint32_t FuncID;
	typedef std::function<void(std::any)> Func_t;
	struct Func {
		Func_t func;
		FuncID ID;
		uint32_t priority; // lower priority runs first
	};

	struct EventSystem
	{
		static EventSystem& get();

		Event createEvent();
		FuncID subscribe(Event event, Func_t&& func, uint32_t priority = -1); // priority set to max unsinged int as default
		void unsubscribe(Event event, FuncID funcID);
		void fire(Event eventID, std::any data = nullptr);

	private:
		std::vector<std::vector<Func>> m_subscribers{};

		EventSystem() {};
		EventSystem(EventSystem const&) {};
		EventSystem& operator=(EventSystem const&) {};
		~EventSystem() {};
	};
}