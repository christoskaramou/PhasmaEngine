#pragma once
#include <vector>
#include <string>
#include <future>
#include <tuple>
#include <thread>

namespace vm {
	struct Queue
	{
		static std::vector<std::tuple<std::string, std::string>> loadModel;
		static std::vector<std::future<void>> func;
	};
}