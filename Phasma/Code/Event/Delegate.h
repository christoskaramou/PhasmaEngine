#pragma once

#include <vector>
#include <functional>
#include <utility>

namespace pe
{
	template<typename ... T>
	class Delegate
	{
	public:
		using Func_type = std::function<void(const T& ...)>;
		
		inline void operator+=(Func_type&& func)
		{
			const size_t funcAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&func));
			
			bool alreadyExist = false;
			for (auto& function : m_functions)
			{
				const size_t functionAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&function));
				
				// This equality seems to work, holding back though since it might not be accurate
				if (funcAddress == functionAddress)
					alreadyExist = true;
			}
			
			if (!alreadyExist)
				m_functions.push_back(std::forward<Func_type>(func));
		}
		
		inline void operator-=(Func_type&& func)
		{
			const size_t funcAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&func));
			
			int index = 0;
			for (auto& function : m_functions)
			{
				const size_t functionAddress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&function));
				
				// This equality seems to work, holding back though since it might not be accurate
				if (funcAddress == functionAddress)
					m_functions.erase(m_functions.begin() + index);
				else
					index++;
			}
		}
		
		inline void Invoke(const T& ... args)
		{
			for (auto& function : m_functions)
				function(args...);
		}
	
	private:
		std::vector<Func_type> m_functions {};
	};
}