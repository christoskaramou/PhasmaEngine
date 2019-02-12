#pragma once

#include <vector>
#include <map>
#include <functional>
#include <cstdarg>

namespace vm {
	struct Event
	{
		static Event& get() {
			static Event instance;
			return instance;
		}

		typedef std::function<void(void*, uint32_t)> subscriber;
		typedef std::function<void*(void*, uint32_t)> subscriber_return;

	private:
		std::map<int, std::vector<subscriber>> m_subscribers{};
		std::map<int, std::vector<subscriber_return>> m_subscribers_return{};

		Event() {};
		Event(Event const&) {};
		Event& operator=(Event const&) {};
		~Event() {};
	};
}