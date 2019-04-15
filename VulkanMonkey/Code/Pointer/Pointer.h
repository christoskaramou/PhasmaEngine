#pragma once
#include <type_traits>

namespace vm {

	template<typename T, typename std::enable_if<!std::is_pointer<T>::value >::type* = 0>
	struct Pointer
	{
		uint32_t hLeft;
		uint32_t hRight;
		T* ptr = nullptr;

		Pointer() : hLeft(0), hRight(0), ptr(nullptr)
		{}

		Pointer(T* const ptr)
		{
			this->ptr = ptr;
			if (ptr) {
				hLeft = (uint32_t)reinterpret_cast<uint64_t>(ptr);
				hRight = (uint32_t)(reinterpret_cast<uint64_t>(ptr) >> 32);
			}
			else
			{
				hLeft = 0;
				hRight = 0;
			}
		}

		T* operator->() {
			return ptr;
		}

		bool operator!() {
			return hLeft + hRight == 0;
		}

		T* get() {
			return ptr;
		}

		static Pointer split(const T* ptr) {
			if (!ptr) return{};
			return { (uint32_t)reinterpret_cast<uint64_t>(ptr), (uint32_t)(reinterpret_cast<uint64_t>(ptr) >> 32), ptr };
		}
		static T* join(const Pointer& ptr) {
			if (!ptr) return nullptr;
			return reinterpret_cast<T*>(((uint64_t)hLeft) << 32 | hRight);
		}
	};
}