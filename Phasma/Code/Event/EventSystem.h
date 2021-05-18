#pragma once

#include "Delegate.h"
#include <map>
#include <deque>
#include <utility>
#include <any>

namespace pe
{
	enum class EventType
	{
		Custom
	};

	using Func = Delegate<std::any>::Func_type;

	class EventSystem
	{
	public:
		// Immediate dispatch
		void DispatchEvent(EventType type, const std::any& data);

		// Add all events of this type to a list so ProcessEvents() process them
		void SendEvent(EventType type, const std::any& data);

		void RegisterEvent(EventType type);

		void UnregisterEvent(EventType type);

		void RegisterEventAction(EventType type, Func&& func);

		void UnregisterEventAction(EventType type, Func&& func);

		void ProcessEvents();

		void ClearEvents();

	private:
		std::unordered_map<EventType, Delegate<std::any>> m_events;
		std::deque<std::pair<Delegate<std::any>*, std::any>> m_processEvents;

	public:
		static auto get()
		{
			static auto instance = new EventSystem();
			return instance;
		}

		EventSystem(EventSystem const&) = delete;                // copy constructor
		EventSystem(EventSystem&&) noexcept = delete;            // move constructor
		EventSystem& operator=(EventSystem const&) = delete;    // copy assignment
		EventSystem& operator=(EventSystem&&) = delete;            // move assignment
		~EventSystem() = default;                                // destructor
	private:
		EventSystem() = default;                                // default constructor
	};
}
