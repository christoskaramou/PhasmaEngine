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

    class Entity : public Node
    {
    public:
        Entity() : m_context(nullptr), m_id(NextID()), m_enabled(false)
        {}

        virtual ~Entity()
        {}

        size_t GetID() const
        { return m_id; }

        Context *GetContext()
        { return m_context; }

        void SetContext(Context *context)
        { m_context = context; }

        bool IsEnabled()
        { return m_enabled; }

        void SetEnabled(bool enabled)
        { m_enabled = enabled; }

        template<class T>
        inline bool HasComponent();

        template<class T>
        inline T *GetComponent();

        template<class T, class... Params>
        inline T *CreateComponent(Params &&... params);

        template<class T>
        inline void RemoveComponent();

    private:
        size_t m_id;
        Context *m_context;
        std::unordered_map <size_t, std::shared_ptr<IComponent>> m_components;
        bool m_enabled;
    };

    template<class T>
    inline bool Entity::HasComponent()
    {
        ValidateBaseClass<IComponent, T>();

        if (m_components.find(GetTypeID<T>()) != m_components.end())
            return true;
        else
            return false;
    }

    template<class T>
    inline T *Entity::GetComponent()
    {
        if (HasComponent<T>())
            return static_cast<T *>(m_components[GetTypeID<T>()].get());
        else
            return nullptr;

    }

    template<class T, class... Params>
    inline T *Entity::CreateComponent(Params &&... params)
    {
        if (!HasComponent<T>())
        {
            size_t id = GetTypeID<T>();
            m_components[id] = std::make_shared<T>(std::forward<Params>(params)...);
            m_components[id]->SetEntity(this);
            m_components[id]->SetEnabled(true);

            return static_cast<T *>(m_components[id].get());
        }
        else
        {
            return nullptr;
        }
    }

    template<class T>
    inline void Entity::RemoveComponent()
    {
        if (HasComponent<T>())
            m_components.erase(GetTypeID<T>());
    }
}
