/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <string>
#include <future>
#include <tuple>
#include <deque>
#include <any>
#include <mutex>
#include "Core/Delegate.h"

namespace pe
{
	enum class Launch
	{
		Async,
		AsyncDeferred,
		AsyncNoWait, // Does not block the main thread, useful for loading. 
		Sync,
		SyncDeferred,
	};
	
	template<Launch launch>
	class Queue
	{
	public:
		using Func = std::function<void()>;

		inline static void Request(Func&& func)
		{
			std::lock_guard<std::mutex> guard(s_requestMutex);

			if constexpr(launch == Launch::Async)
			{
				s_futures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
				s_requests += std::bind(&std::future<void>::get, &s_futures.back());
			}
			else if constexpr (launch == Launch::AsyncDeferred)
			{
				s_futures.push_back(std::async(std::launch::deferred, std::forward<Func>(func)));
				s_requests += std::bind(&std::future<void>::get, &s_futures.back());
			}
			else if constexpr (launch == Launch::AsyncNoWait)
			{
				s_noWaitFutures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
			}
			else if constexpr (launch == Launch::Sync)
			{
				std::forward<Func>(func)();
			}
			else // if constexpr (launch == Launch::SyncDeferred)
			{
				s_requests += std::forward<Func>(func);
			}
		}

		inline static void ExecuteRequests()
		{
			CheckNoWaitFutures();
			s_requests.ReverseInvoke();
			s_requests.Clear();
			s_futures.clear();
		}

	private:
		inline static void CheckNoWaitFutures()
		{
			for (auto it = s_noWaitFutures.begin(); it != s_noWaitFutures.end();)
			{
				if (it->wait_for(std::chrono::seconds(0)) != std::future_status::timeout)
				{
					it->get();
					it = s_noWaitFutures.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

	private:
		inline static Delegate<> s_requests{};
		inline static std::deque<std::future<void>> s_futures;
		inline static std::deque<std::future<void>> s_noWaitFutures;
		inline static std::mutex s_requestMutex;
	};
}