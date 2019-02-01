#pragma once
#include <string>
#include <future>
#include <tuple>
#include <thread>
#include <deque>

namespace vm {
	struct Queue
	{
		static std::deque<std::tuple<std::string, std::string>> loadModel;
		static std::deque<std::future<void>> func;
	};
}