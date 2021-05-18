#pragma once

#include <vector>
#include <functional>
#include <utility>

namespace pe
{
	template <typename ... T>
	class Delegate
	{
	public:
		using Func_type = std::function<void(const T& ...)>;

		inline void operator+=(Func_type&& func)
		{
			const size_t funcAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&func));

			bool alreadyExist = false;
			for (auto& function : delegates)
			{
				const size_t functionAdress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&function));

				// This equality seems to work, holding back though since it might not be accurate
				if (funcAddress == functionAdress)
					alreadyExist = true;
			}

			if (!alreadyExist)
				delegates.push_back(std::forward<Func_type>(func));
		}

		inline void operator-=(Func_type&& func)
		{
			const size_t funcAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&func));

			int index = 0;
			for (auto& function : delegates)
			{
				const size_t functionAdress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&function));

				// This equality seems to work, holding back though since it might not be accurate
				if (funcAddress == functionAdress)
					delegates.erase(delegates.begin() + index);
				else
					index++;
			}
		}

		inline void Invoke(const T& ... args)
		{
			for (auto& func : delegates)
				func(args...);
		}

	private:
		std::vector<Func_type> delegates {};
	};
}