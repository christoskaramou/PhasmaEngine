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
    class EventSystem
    {
    public:
        using Func = Delegate<std::any>::Func_type;
        
        static void Init();

        static void Destroy();

        // DISPATCHING EVENTS ----------------------------------
        static void RegisterEvent(EventID event);

        static void UnregisterEvent(EventID type);

        static void RegisterCallback(EventID type, Func &&func);

        static void UnregisterEventCallback(EventID type, Func &&func);
        
        static void DispatchEvent(EventID type, std::any &&data);

        static void ClearEvents();
        // -----------------------------------------------------

        // POLLING EVENTS --------------------------------------
        // Push event have to be polled manually
        static void PushEvent(EventID type);

        // Poll a pushed event
        static bool PollEvent(EventID type);

        static void ClearPushedEvents();
        // -----------------------------------------------------

    private:
        inline static std::unordered_map<size_t, Delegate<std::any>> m_events{};
        inline static std::unordered_set<size_t> m_pushEvents{};
    };
}
