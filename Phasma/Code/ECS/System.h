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
    class Context;
    class IComponent;
    class CommandBuffer;

    class ISystem
    {
    public:
        ISystem() : m_enabled(false) {}

        virtual ~ISystem() {}

        virtual void Init(CommandBuffer *cmd) = 0;

        virtual void Update(double delta) = 0;

        virtual void Destroy() = 0;

        bool IsEnabled() { return m_enabled; }

        void SetEnabled(bool enabled) { m_enabled = enabled; }

        template <class T>
        inline bool HasComponents()
        {
            ValidateBaseClass<IComponent, T>();

            if (m_components.find(GetTypeID<T>()) != m_components.end())
                return true;

            return false;
        }

        template <class T>
        inline void AttachComponent(T *component)
        {
            size_t id = GetTypeID<T>();
            if (!HasComponents<T>())
                m_components[id] = std::vector<IComponent *>();
            m_components[id].push_back(component);
        }

        template <class T>
        void RemoveComponent(IComponent *component)
        {
            if (HasComponents<T>())
            {
                size_t id = GetTypeID<T>();
                auto it = std::find(m_components[id].begin(), m_components[id].end(), component);
                if (it != m_components[id].end())
                    m_components[id].erase(it);
            }
        }

        template <class T>
        void RemoveComponents()
        {
            if (HasComponents<T>())
                m_components[GetTypeID<T>()].clear();
        }

        template <class T>
        void RemoveAllComponents()
        {
            m_components.clear();
        }

        template <class T>
        const std::vector<T *> &GetComponentsOfType()
        {
            if (HasComponents<T>())
            {
                size_t id = GetTypeID<T>();
                return *reinterpret_cast<std::vector<T *> *>(&m_components[id]);
            }

            static const std::vector<T *> empty{};
            return empty;
        }

    private:
        std::unordered_map<size_t, std::vector<IComponent *>> m_components;
        bool m_enabled;
    };

    class IDrawSystem : public ISystem
    {
    public:
        virtual void Draw() = 0;
    };
}
