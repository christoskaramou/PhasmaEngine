#pragma once
#include <type_traits>

namespace vm {

	template<typename T, typename std::enable_if<!std::is_pointer<T>::value >::type* = 0>
	struct Pointer
	{
		uint32_t hLeft;
		uint32_t hRight;
		T* ptr;

		Pointer() : hLeft(0), hRight(0), ptr(nullptr)
		{}

		Pointer(T* const ptr)
		{
			this->ptr = ptr;
			if (ptr) {
				hLeft = static_cast<uint32_t>(reinterpret_cast<uint64_t>(ptr));
				hRight = static_cast<uint32_t>(reinterpret_cast<uint64_t>(ptr) >> 32);
			}
			else
			{
				hLeft = 0;
				hRight = 0;
			}
		}

		T* operator->() {
			// if (!ptr) throw std::runtime_error("reference to a null pointer");
			return ptr;
		}
		operator bool() {
			return ptr != nullptr && hLeft + hRight != 0;
		}

		bool operator!() {
			return !operator bool();
		}

		T* get() {
			return ptr;
		}

		static Pointer split(const T* ptr) {
			if (!ptr) return{};
			return { static_cast<uint32_t>(reinterpret_cast<uint64_t>(ptr)), static_cast<uint32_t>(reinterpret_cast<uint64_t>(ptr) >> 32), ptr };
		}
		static T* join(const Pointer& ptr) {
			if (!ptr) return nullptr;
			return reinterpret_cast<T*>((static_cast<uint64_t>(ptr.hLeft)) << 32 | ptr.hRight);
		}
	};
}