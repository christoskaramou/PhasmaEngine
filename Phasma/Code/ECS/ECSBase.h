#pragma once

#include <algorithm>
#include <execution>

namespace pe
{
	inline size_t NextID()
	{
		static size_t ID = 0;
		return ID++;
	}
	
	template<class T>
	inline size_t GetTypeID()
	{
		static size_t typeID = NextID();
		return typeID;
	}
	
	template<class T, class U>
	constexpr void ValidateBaseClass()
	{
		static_assert(
				std::is_base_of<T, U>::value, "ValidateBaseClass<T, U>(): \"T is not the base class of U\" assertion"
		);
	}
	
	template<class T>
	void ForEachParallel(const std::vector<T>& container, void(* func)(T))
	{
		if (container.size() > 3)
		{
			std::for_each(std::execution::par_unseq, container.begin(), container.end(), func);
		}
		else
		{
			for (auto& elem : container)
				func(elem);
		}
	}
	
	class BaseBehaviour
	{
	public:
		virtual ~BaseBehaviour()
		{}
		
		virtual void Init()
		{}
		
		virtual void Update(double delta)
		{}
		
		virtual void Draw()
		{}
		
		virtual void FixedUpdate()
		{}
		
		virtual void OnGUI()
		{}
		
		virtual void OnEnable()
		{}
		
		virtual void OnDisable()
		{}
		
		virtual void Destroy()
		{}
	};
}
