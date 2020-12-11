#pragma once
#include <memory>
#include <vector>

namespace vm
{
	template<class T>
	using Ref = std::shared_ptr<T>;

	template <class T>
	Ref<T> make_ref(const T& obj)
	{
		return std::make_shared<T>(obj);
	}

	template <class T>
	std::vector<Ref<T>> make_ref_vec(const std::vector<T>& vec)
	{
		std::vector ref_vec(vec.size());

		for (int i = 0; i < vec.size(); i++)
			ref_vec[i] = std::make_shared<T>(vec[i]);

		return ref_vec;
	}
}