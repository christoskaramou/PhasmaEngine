#pragma once

namespace pe
{
    class RHI;

    class Context final
    {
    public:
        static auto Get()
        {
            static auto instance = new Context();
            return instance;
        }

        Context(Context const &) = delete; // copy constructor
        Context(Context &&) noexcept = delete; // move constructor
        ~Context() = default; // destructor
        Context &operator=(Context const &) = delete; // copy assignment
        Context &operator=(Context &&) = delete; // move assignment

    private:
        Context();

    public:
        void InitSystems();
        void DestroySystems();
        void UpdateSystems();
        void DrawSystems();

        template <class T, class... Params>
        inline T *CreateSystem(Params &&...params);
        template <class T>
        inline T *GetSystem();
        template <class T>
        inline void RemoveSystem();

        Entity *CreateEntity();
        Entity *GetEntity(size_t id);
        void RemoveEntity(size_t id);
        Entity *GetWorldEntity() { return m_worldEntity; }
        std::unordered_map<size_t, std::shared_ptr<ISystem>> GetSystems();

    private:
        Entity *m_worldEntity;

        std::unordered_map<size_t, std::shared_ptr<ISystem>> m_systems;
        std::unordered_map<size_t, IDrawSystem *> m_drawSystems;
        std::unordered_map<size_t, std::shared_ptr<Entity>> m_entities;
    };

    template <class T, class... Params>
    inline T *Context::CreateSystem(Params &&...params)
    {
        ValidateBaseClass<ISystem, T>();

        size_t id = ID::GetTypeID<T>();
        auto it = m_systems.find(id);
        if (it == m_systems.end())
        {
            auto system = std::make_shared<T>(std::forward<Params>(params)...);
            system->SetEnabled(true);
            m_systems[id] = system;

            if constexpr (std::is_base_of<IDrawSystem, T>::value)
                m_drawSystems[id] = static_cast<IDrawSystem *>(system.get());

            return static_cast<T *>(system.get());
        }
        return static_cast<T *>(it->second.get());
    }

    template <class T>
    inline T *Context::GetSystem()
    {
        ValidateBaseClass<ISystem, T>();

        auto it = m_systems.find(ID::GetTypeID<T>());
        if (it != m_systems.end())
            return static_cast<T *>(it->second.get());

        PE_ERROR("System not found");
        return nullptr;
    }

    template <class T>
    inline void Context::RemoveSystem()
    {
        ValidateBaseClass<ISystem, T>();

        auto it = m_systems.find(ID::GetTypeID<T>());
        if (it != m_systems.end())
        {
            it->second->Destroy();
            m_systems.erase(it);
        }
    }

    template <class T>
    inline T *CreateGlobalComponent()
    {
        return Context::Get()->GetWorldEntity()->CreateComponent<T>();
    }

    template <class T>
    inline T *GetGlobalComponent()
    {
        return Context::Get()->GetWorldEntity()->GetComponent<T>();
    }

    inline uint32_t GetGlobalComponentCount()
    {
        return Context::Get()->GetWorldEntity()->GetComponentCount();
    }
    
    template <class T>
    inline T *CreateGlobalSystem()
    {
        return Context::Get()->CreateSystem<T>();
    }

    inline void DestroyGlobalSystems()
    {
        Context::Get()->DestroySystems();
    }

    template <class T>
    inline T *GetGlobalSystem()
    {
        return Context::Get()->GetSystem<T>();
    }

    void UpdateGlobalSystems();

    void DrawGlobalSystems();
}
