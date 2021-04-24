#pragma once

#include <memory>

namespace vm
{
	template <class T>
	using Ref = std::shared_ptr<T>;

	template <class T>
	Ref<T> make_ref(T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template <class T>
	Ref<T> make_ref(const T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template <class T>
	Ref<T> make_ref(T&& obj)
	{
		return std::make_shared<T>(std::forward<T>(obj));
	}
}