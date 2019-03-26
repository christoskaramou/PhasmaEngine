#pragma once
#include <string>
#include <future>
#include <tuple>
#include <thread>
#include <deque>

namespace vm {
	struct Queue
	{
		// std::deque insertion and deletion at either end of a deque never invalidates pointers or references to the rest of the elements
		static std::deque<std::tuple<std::string, std::string>> loadModel;
		static std::deque<std::tuple<int, std::string>> addScript;
		static std::deque<std::future<void>> func;
	};
}