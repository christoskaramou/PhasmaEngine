#pragma once

#include <memory>

namespace pe
{
	template<class T>
	using Ref = std::shared_ptr<T>;
	
	template<class T>
	Ref<T> make_ref(T& obj)
	{
		return std::make_shared<T>(obj);
	}
	
	template<class T>
	Ref<T> make_ref(const T& obj)
	{
		return std::make_shared<T>(obj);
	}
	
	template<class T>
	Ref<T> make_ref(T&& obj)
	{
		return std::make_shared<T>(std::forward<T>(obj));
	}
	
	class NoCopy
	{
	public:
		NoCopy() = default;
		
		NoCopy(const NoCopy&) = delete;
		
		NoCopy& operator=(const NoCopy&) = delete;
		
		NoCopy(NoCopy&&) = default;
		
		NoCopy& operator=(NoCopy&&) = default;
	};
	
	class NoMove
	{
	public:
		NoMove() = default;
		
		NoMove(const NoMove&) = default;
		
		NoMove& operator=(const NoMove&) = default;
		
		NoMove(NoMove&&) = delete;
		
		NoMove& operator=(NoMove&&) = delete;
	};
}