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

		inline static void Request(Func&& func, Func&& update = nullptr, Func&& signal = nullptr)
		{
			std::lock_guard<std::mutex> guard(s_requestMutex);

			if constexpr(launch == Launch::Async)
			{
				if (update)
					update();
				s_futures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
				s_signals.push_back(std::forward<Func>(signal));
			}
			else if constexpr (launch == Launch::AsyncDeferred)
			{
				if (update)
					update();
				s_futures.push_back(std::async(std::launch::deferred, std::forward<Func>(func)));
				s_signals.push_back(std::forward<Func>(signal));
			}
			else if constexpr (launch == Launch::AsyncNoWait)
			{
				if (update)
					update();
				s_updatesNoWait.push_back(std::forward<Func>(update));
				s_noWaitFutures.push_back(std::async(std::launch::async, std::forward<Func>(func)));
				s_signalsNoWait.push_back(std::forward<Func>(signal));
			}
			else if constexpr (launch == Launch::Sync)
			{
				if (update)
					update();
				std::forward<Func>(func)();
				if (signal)
					signal();
			}
			else // if constexpr (launch == Launch::SyncDeferred)
			{
				s_requests += std::forward<Func>(func);
			}
		}

		inline static void ExecuteRequests()
		{
			CheckNoWaitFutures();

			for (int i = static_cast<int>(s_futures.size()) - 1; i >= 0; i--)
			{
				s_futures[i].get();

				if (s_signals[i])
					s_signals[i]();
			}

			s_futures.clear();
			s_signals.clear();
		}

	private:
		inline static void CheckNoWaitFutures()
		{
			int i = 0;
			for (auto it = s_noWaitFutures.begin(); it != s_noWaitFutures.end();)
			{
				if (it->wait_for(std::chrono::seconds(0)) != std::future_status::timeout)
				{
					it->get();
					it = s_noWaitFutures.erase(it);

					if (s_signalsNoWait[i])
						s_signalsNoWait[i]();

					s_updatesNoWait.erase(s_updatesNoWait.begin() + i);
					s_signalsNoWait.erase(s_signalsNoWait.begin() + i);
				}
				else
				{
					if (s_updatesNoWait[i])
						s_updatesNoWait[i]();

					++it;
					++i;
				}
			}
		}

	private:
		inline static std::deque<std::future<void>> s_futures;
		inline static std::deque<Func> s_signals;
		inline static std::deque<std::future<void>> s_noWaitFutures;
		inline static std::deque<Func> s_updatesNoWait;
		inline static std::deque<Func> s_signalsNoWait;
		inline static std::mutex s_requestMutex;
	};
}