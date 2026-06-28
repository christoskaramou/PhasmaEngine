#pragma once

namespace pe
{
    template <typename T>
    class ResourceHandle
    {
    public:
        ResourceHandle() : m_ptr(nullptr) {}
        ResourceHandle(std::shared_ptr<T> ptr) : m_ptr(ptr) {}

        static ResourceHandle<T> FromRaw(T *ptr)
        {
            if (!ptr)
                return ResourceHandle<T>();
            return ResourceHandle<T>(std::shared_ptr<T>(ptr, [](T *) {}));
        }

        T *get() const { return m_ptr.get(); }
        T *operator->() const { return m_ptr.get(); }
        T &operator*() const { return *m_ptr; }
        explicit operator bool() const { return m_ptr != nullptr; }

        bool operator==(const ResourceHandle<T> &other) const { return m_ptr == other.m_ptr; }
        bool operator!=(const ResourceHandle<T> &other) const { return m_ptr != other.m_ptr; }

        std::shared_ptr<T> GetShared() const { return m_ptr; }

    private:
        std::shared_ptr<T> m_ptr;
    };

    class ResourceManager
    {
    public:
        static ResourceManager &Get()
        {
            static ResourceManager instance;
            return instance;
        }

        template <typename T, typename... Args>
        ResourceHandle<T> Load(const std::string &id, Args &&...args)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto typeIdx = std::type_index(typeid(T));

            auto &cache = m_resources[typeIdx];
            auto it = cache.find(id);

            if (it != cache.end())
            {
                std::shared_ptr<Resource> res = it->second.lock();
                if (res)
                {
                    return ResourceHandle<T>(std::static_pointer_cast<T>(res));
                }
            }

            // Not found or expired, create new
            std::shared_ptr<T> newResource = std::make_shared<T>(std::forward<Args>(args)...);
            newResource->SetResourceId(id);
            newResource->Load();

            cache[id] = newResource; // Store weak_ptr

            return ResourceHandle<T>(newResource);
        }

        template <typename T>
        ResourceHandle<T> Find(const std::string &id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto typeIdx = std::type_index(typeid(T));
            auto typeIt = m_resources.find(typeIdx);
            if (typeIt == m_resources.end())
                return ResourceHandle<T>();

            auto &cache = typeIt->second;
            auto it = cache.find(id);
            if (it == cache.end())
                return ResourceHandle<T>();

            std::shared_ptr<Resource> res = it->second.lock();
            if (!res)
                return ResourceHandle<T>();

            return ResourceHandle<T>(std::static_pointer_cast<T>(res));
        }

        template <typename T>
        void Register(const std::string &id, std::shared_ptr<T> resource)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto typeIdx = std::type_index(typeid(T));
            resource->SetResourceId(id);
            m_resources[typeIdx][id] = resource;
        }

    private:
        ResourceManager() = default;
        ~ResourceManager() = default;
        ResourceManager(const ResourceManager &) = delete;
        ResourceManager &operator=(const ResourceManager &) = delete;

        std::mutex m_mutex;
        // Type Index -> (string id -> weak_ptr<Resource>)
        // We use weak_ptr so that the ResourceManager doesn't keep resources alive on its own.
        // Resources are automatically destroyed when all ResourceHandles are gone.
        std::unordered_map<std::type_index, std::unordered_map<std::string, std::weak_ptr<Resource>>> m_resources;
    };
} // namespace pe
