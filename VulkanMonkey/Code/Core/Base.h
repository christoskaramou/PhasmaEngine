#pragma once
#include <memory>

namespace vm
{
	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	template<class T>
	class Ref_t
	{
	public:
		void operator=(const Ref<T>& ref) { m_ref = ref; }
		T& operator*() const { return *m_ref; }
		Ref<T> operator->() const { return m_ref; }
		Ref<T> GetRef() const { return m_ref; };
		void SetRef(const Ref<T>& ref) { m_ref = ref; };
	protected:
		Ref<T> m_ref;
	};
}