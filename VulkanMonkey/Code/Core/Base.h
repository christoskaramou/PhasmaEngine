#pragma once
#include <memory>

namespace vm
{
	template<class T>
	class Ref_t
	{
	public:
		void operator=(const T& obj) { m_ref = std::make_shared<T>(obj); }
		void operator=(const std::shared_ptr<T>& ref) { m_ref = ref; }
		T& operator*() const { return *m_ref; }
		const T& Value() const { return *m_ref; }
		std::shared_ptr<T> operator->() const { return m_ref; }
		std::shared_ptr<T> GetRef() const { return m_ref; };
		void SetRef(const T& obj) { m_ref = std::make_shared<T>(obj); };
		void SetRef(const std::shared_ptr<T>& ref) { m_ref = ref; };
		void Invalidate() { m_ref = nullptr; };
	protected:
		std::shared_ptr<T> m_ref;
	};
}