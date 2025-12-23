#pragma once

namespace pe
{
    class Context;
    class IComponent;
    class CommandBuffer;

    class ISystem
    {
    public:
        ISystem() noexcept : m_enabled(false) {}
        virtual ~ISystem() = default;

        virtual void Init(CommandBuffer *cmd) = 0;
        virtual void Update() = 0;
        virtual void Destroy() = 0;

        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        template <class T>
        inline void AttachComponent(T *component)
        {
            size_t id = ID::GetTypeID<T>();
            auto it = m_components.find(id);

            if (it == m_components.end())
                it = m_components.emplace(id, std::vector<IComponent *>()).first;

            it->second.push_back(component);
        }

        template <class T>
        void RemoveComponent(IComponent *component)
        {
            ValidateBaseClass<IComponent, T>();

            auto id = ID::GetTypeID<T>();
            auto map_it = m_components.find(id);

            if (map_it != m_components.end())
            {
                auto &vec = map_it->second;
                auto vec_it = std::find(vec.begin(), vec.end(), component);
                if (vec_it != vec.end())
                    vec.erase(vec_it);
            }
        }

        template <class T>
        void RemoveComponents()
        {
            ValidateBaseClass<IComponent, T>();

            auto id = ID::GetTypeID<T>();
            auto it = m_components.find(id);
            if (it != m_components.end())
                it->second.clear();
        }

        void RemoveAllComponents() noexcept
        {
            m_components.clear();
        }

        template <class T>
        const std::vector<T *> &GetComponentsOfType()
        {
            ValidateBaseClass<IComponent, T>();

            static const std::vector<T *> empty{};
            auto id = ID::GetTypeID<T>();
            auto it = m_components.find(id);

            if (it != m_components.end())
                return *reinterpret_cast<const std::vector<T *> *>(&it->second);

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
} // namespace pe
