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

namespace pe
{
    enum class FenceStatus
    {
        Signaled,
        Unsignaled,
        Error
    };

    class Fence : public IHandle<Fence, FenceHandle>
    {
    public:
        Fence(bool signaled, const std::string &name = {});

        ~Fence();

        void Wait();

        void Reset();

        FenceStatus GetStatus();

        static void Init();

        static void Clear();

        static Fence *GetNext();

        static void Return(Fence *fence);

        std::string name;

    private:
        static void CheckReturns();

        inline static std::unordered_map<Fence *,Fence *> s_availableFences{};
        inline static std::unordered_map<Fence *,Fence *> s_returnFences{};
        inline static std::map<Fence *, Fence *> s_allFences{};

        friend class Queue;
        friend class CommandBuffer;
        std::atomic_bool m_canReturnToPool = false;
        std::atomic_bool m_submitted = false;
    };
}