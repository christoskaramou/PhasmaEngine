#pragma once

#include <memory>
#include <type_traits>

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
	
	template<class T>
	class Flags
	{
	public:
		using MaskType = typename std::underlying_type<T>::type;
		
		Flags()
		{
			m_mask = 0;
		}
		
		Flags(const MaskType& mask)
		{
			m_mask = mask;
		}
		
		//Flags(const T& mask)
		//{
		//
		//}
		
		constexpr Flags<T> operator&(Flags<T> const& rhs) const noexcept
		{
			return Flags<T>(m_mask & rhs.m_mask);
		}
		
		constexpr Flags<T> operator|(Flags<T> const& rhs) const noexcept
		{
			return Flags<T>(m_mask | rhs.m_mask);
		}
		
		explicit constexpr operator bool() const noexcept
		{
			return !!m_mask;
		}
		
		explicit constexpr operator MaskType() const noexcept
		{
			return m_mask;
		}
		
		constexpr Flags<T> & operator=(const Flags<T>& rhs) noexcept = default;
		
		constexpr Flags<T> & operator|=(Flags<T> const& rhs) noexcept
		{
			m_mask |= rhs.m_mask;
			return *this;
		}
		
		constexpr Flags<T> & operator&=(Flags<T> const& rhs) noexcept
		{
			m_mask &= rhs.m_mask;
			return *this;
		}
		
	private:
		MaskType m_mask;
	};
	
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