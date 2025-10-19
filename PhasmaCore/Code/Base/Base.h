#pragma once

namespace pe
{
    namespace ID
    {
        inline size_t NextID()
        {
            static std::atomic_size_t id{0};
            return id.fetch_add(1, std::memory_order_relaxed);
        }

        // Compile time type id
        template <class T>
        struct TypeIDHelper
        {
            static constexpr char value{};
        };

        template <class T>
        struct remove_all_pointers_and_references
        {
            using type = std::remove_cv_t<std::remove_reference_t<T>>;
        };

        template <class T>
        struct remove_all_pointers_and_references<T *> : remove_all_pointers_and_references<T>
        {
        };

        template <class T>
        using remove_all_pointers_and_references_t = typename remove_all_pointers_and_references<T>::type;

        template <class T>
        constexpr size_t GetTypeID()
        {
            static_assert(sizeof(size_t) >= sizeof(void *), "size_t is too small to hold a pointer");
            using CleanType = remove_all_pointers_and_references_t<T>;
            return reinterpret_cast<size_t>(&TypeIDHelper<CleanType>::value);
        }

        template <class T>
        constexpr size_t GetTypeID(T &&)
        {
            return GetTypeID<std::remove_reference_t<T>>();
        }
    }

    template <uint32_t N>
    struct Placeholder
    {
    };

    class NoCopy
    {
    public:
        NoCopy() = default;
        NoCopy(const NoCopy &) = delete;
        NoCopy &operator=(const NoCopy &) = delete;
        NoCopy(NoCopy &&) = default;
        NoCopy &operator=(NoCopy &&) = default;
    };

    class NoMove
    {
    public:
        NoMove() = default;
        NoMove(const NoMove &) = default;
        NoMove &operator=(const NoMove &) = default;
        NoMove(NoMove &&) = delete;
        NoMove &operator=(NoMove &&) = delete;
    };

    // Maintains the order of items
    template <class Key, class Value>
    class OrderedMap
    {
    public:
        using ListType = std::list<Value>;
        using iterator = typename ListType::iterator;
        using const_iterator = typename ListType::const_iterator;
        using reverse_iterator = typename ListType::reverse_iterator;
        using const_reverse_iterator = typename ListType::const_reverse_iterator;

        iterator begin() { return m_list.begin(); }
        iterator end() { return m_list.end(); }
        reverse_iterator rbegin() { return m_list.rbegin(); }
        reverse_iterator rend() { return m_list.rend(); }
        const_iterator cbegin() const { return m_list.cbegin(); }
        const_iterator cend() const { return m_list.cend(); }
        const_reverse_iterator crbegin() const { return m_list.crbegin(); }
        const_reverse_iterator crend() const { return m_list.crend(); }

        OrderedMap() = default;

        OrderedMap(size_t capacity)
        {
            m_map.reserve(capacity);
        }

        OrderedMap(std::initializer_list<std::pair<const Key, Value>> init)
        {
            for (const auto &item : init)
            {
                insert(item.first, item.second);
            }
        }

        size_t size() const { return m_list.size(); }

        bool contains(const Key &key) const { return m_map.find(key) != m_map.end(); }

        iterator find(const Key &key)
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                return it->second;
            }
            return m_list.end();
        }

        const_iterator find(const Key &key) const
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                return it->second;
            }
            return m_list.end();
        }

        Value &get(const Key &key)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                throw std::out_of_range("Key not found in map");
            }
            return *(it->second);
        }

        const Value &get(const Key &key) const
        {
            return *(m_map.at(key));
        }

        template <class... Args>
        std::pair<bool, iterator> try_emplace(const Key &key, Args &&...args)
        {
            auto it = m_map.find(key);
            if (it != m_map.end())
                return {false, it->second};
            m_list.emplace_back(std::forward<Args>(args)...);
            auto iter = std::prev(m_list.end());
            m_map.emplace(key, iter);
            return {true, iter};
        }

        Value &operator[](const Key &key)
        {
            auto [inserted, it] = try_emplace(key, Value{});
            (void)inserted;
            return *it;
        }

        const Value &operator[](const Key &key) const
        {
            return *(m_map.at(key));
        }

        std::pair<bool, std::optional<iterator>> insert(const Key &key, const Value &value)
        {
            auto it = m_map.find(key);
            if (it == m_map.end())
            {
                m_list.push_back(value);
                auto iter = m_map.emplace_hint(m_map.end(), key, std::prev(m_list.end()));
                return {true, iter->second};
            }
            return {false, std::nullopt};
        }

        bool erase(const Key &key)
        {
            auto map_it = m_map.find(key);
            if (map_it != m_map.end())
            {
                m_list.erase(map_it->second);
                m_map.erase(map_it);
                return true;
            }
            return false;
        }

        void clear()
        {
            m_list.clear();
            m_map.clear();
        }

    private:
        ListType m_list;
        std::unordered_map<Key, iterator> m_map;
    };

    class PeHandleBase
    {
    public:
        PeHandleBase() {}
        virtual ~PeHandleBase() {}
    };

    // Manages a class that contains handles
    // Disallows moving the actual objects and prodiving a Create and Destroy funtion for every PeHandle,
    // which manages the memory for heap allocations through Create
    //
    // Important: Every PeHandle should provide a constructor and a destructor managing the
    // create and destroy of the API_HANDLE
    template <class T, class API_HANDLE>
    class PeHandle : public PeHandleBase, public NoCopy, public NoMove
    {
    public:
        template <class... Params>
        inline static T *Create(Params &&...params)
        {
            ValidateBaseClass<PeHandle<T, API_HANDLE>, T>();
            T *ptr = new T(std::forward<Params>(params)...);

#if defined(PE_TRACK_RESOURCES)
            s_handles.push_back(ptr);
#endif

            return ptr;
        }

        inline static void Destroy(T *&ptr)
        {
            ValidateBaseClass<PeHandle<T, API_HANDLE>, T>();

            if (ptr)
            {
                delete ptr; // should call ~T() destructor

#if defined(PE_TRACK_RESOURCES)
                std::erase(s_handles, ptr);
#endif
            }

            ptr = nullptr;
        }

        virtual ~PeHandle() {}
        API_HANDLE &ApiHandle() { return m_apiHandle; }
        const API_HANDLE &ApiHandle() const { return m_apiHandle; }

#if defined(PE_TRACK_RESOURCES)
        inline static std::deque<T *> s_handles{};
#endif

    protected:
        PeHandle() : m_apiHandle{} {}
        PeHandle(const API_HANDLE &apiHandle) : m_apiHandle{apiHandle} {}

        API_HANDLE m_apiHandle;
    };
}
