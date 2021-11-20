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
    enum class EventType
    {
        Quit,
        Custom,
        SetWindowTitle,
        CompileShaders,
        ScaleRenderTargets,
        FileWrite
    };

    class EventSystem : public ISystem
    {
    public:
        using Func = Delegate<std::any>::Func_type;

        void Init() override;

        void Update(double delta) override;

        void Destroy() override;

        // Immediately dispatch a registered event
        void DispatchEvent(EventType type, std::any&& data);

        void RegisterEvent(EventType type);

        void UnregisterEvent(EventType type);

        void RegisterEventAction(EventType type, Func&& func);

        void UnregisterEventAction(EventType type, Func&& func);

        void PushEvent(EventType type);

        bool PollEvent(EventType type);

        void ClearPushedEvents();

        void ClearEvents();

    private:
        std::unordered_map<EventType, Delegate<std::any>> m_events;
        std::unordered_set<EventType> m_pushedEventTypes;
    };
}
