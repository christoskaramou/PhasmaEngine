#pragma once

namespace pe
{
    class Context;

    class Entity
    {
    public:
        Entity() : m_context(nullptr), m_id(ID::NextID()), m_enabled(false) {}
        virtual ~Entity() = default;

        virtual void Update(float dt);
        virtual void Draw();

        [[nodiscard]] size_t GetID() const noexcept { return m_id; }
        [[nodiscard]] Context *GetContext() const noexcept { return m_context; }
        void SetContext(Context *context) noexcept { m_context = context; }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        template <class T>
        inline T *GetComponent();
        auto &GetComponents() noexcept { return m_activeComponents; }
        [[nodiscard]] uint32_t GetComponentCount() const noexcept { return static_cast<uint32_t>(m_activeComponents.size()); }
        template <class T, class... Params>
        inline T *CreateComponent(Params &&...params);
        template <class T>
        inline void RemoveComponent();

    private:
        size_t m_id;
        Context *m_context;
        std::unordered_map<size_t, IComponent *> m_componentMap;
        std::vector<std::unique_ptr<IComponent>> m_activeComponents;
        bool m_enabled;
    };

    template <class T>
    inline T *Entity::GetComponent()
    {
        ValidateBaseClass<IComponent, T>();

        auto it = m_componentMap.find(ID::GetTypeID<T>());
        if (it != m_componentMap.end())
            return static_cast<T *>(it->second);

        return nullptr;
    }

    template <class T, class... Params>
    inline T *Entity::CreateComponent(Params &&...params)
    {
        ValidateBaseClass<IComponent, T>();

        size_t id = ID::GetTypeID<T>();
        auto it = m_componentMap.find(id);
        if (it != m_componentMap.end())
            return static_cast<T *>(it->second);

        auto comp = std::make_unique<T>(std::forward<Params>(params)...);
        comp->SetEntity(this);
        comp->SetEnabled(true);
        T *ptr = comp.get();

        m_componentMap[id] = ptr;
        m_activeComponents.push_back(std::move(comp));

        return ptr;
    }

    template <class T>
    inline void Entity::RemoveComponent()
    {
        ValidateBaseClass<IComponent, T>();

        size_t id = ID::GetTypeID<T>();
        auto mapIt = m_componentMap.find(id);
        if (mapIt != m_componentMap.end())
        {
            IComponent *ptr = mapIt->second;
            m_componentMap.erase(mapIt);
            auto it = std::find_if(m_activeComponents.begin(), m_activeComponents.end(),
                                   [ptr](const std::unique_ptr<IComponent> &comp)
                                   {
                                       return comp.get() == ptr;
                                   });
            if (it != m_activeComponents.end())
                m_activeComponents.erase(it);
        }
    }

    inline void Entity::Update(float dt)
    {
        if (!m_enabled)
            return;
        for (auto &comp : m_activeComponents)
        {
            if (comp->IsEnabled())
                comp->Update(dt);
        }
    }

    inline void Entity::Draw()
    {
        if (!m_enabled)
            return;
        for (auto &comp : m_activeComponents)
        {
            if (comp->IsEnabled())
                comp->Draw();
        }
    }
} // namespace pe
