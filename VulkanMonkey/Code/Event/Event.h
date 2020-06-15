#pragma once

#include <vector>
#include <functional>
#include <any>
#include <memory>

namespace vm
{
#define CREATE_EVENT EventSystem::get()->createEvent
#define SUBSCRIBE_TO_EVENT EventSystem::get()->subscribe
#define FIRE_EVENT EventSystem::get()->fire
#define UNSUBSCRIBE_FROM_EVENT EventSystem::get()->unsubscribe
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

	using FuncID = uint32_t;
	using Func_t = std::function<void(const std::any&)>;
	struct Func
	{
		Func_t func;
		FuncID ID;
		uint32_t priority; // lower priority runs first
	};

	class EventSystem
	{
	public:
		Event createEvent();
		FuncID subscribe(Event event, Func_t&& func, uint32_t priority = -1); // priority set to max unsigned int as default
		void unsubscribe(Event event, FuncID funcID);
		void fire(Event eventID, const std::any& data = nullptr);

		static auto get() { static auto instance = new EventSystem(); return instance; }
		static auto remove() { using type = decltype(get()); if (std::is_pointer<type>::value) delete get(); }

		EventSystem(EventSystem const&) = delete;				// copy constructor
		EventSystem(EventSystem&&) noexcept = delete;			// move constructor
		EventSystem& operator=(EventSystem const&) = delete;	// copy assignment
		EventSystem& operator=(EventSystem&&) = delete;			// move assignment
	private:
		EventSystem() = default;								// default constructor
		~EventSystem() = default;								// destructor

		std::vector<std::vector<Func>> m_subscribers{};
	};
}
