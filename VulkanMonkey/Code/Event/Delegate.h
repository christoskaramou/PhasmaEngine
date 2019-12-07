#pragma once

#include <vector>
#include <functional>

template<typename ... T>
struct Delegate
{
	using Func_type = std::function<void(const T& ...)>;

	inline void operator += (Func_type&& func) {
		delegates.push_back(std::forward<Func_type>(func));
	}
	inline void invoke(const T& ... args) {
		for (auto& func : delegates)
			func(args...);
	}
private:
	std::vector<Func_type> delegates{};
};