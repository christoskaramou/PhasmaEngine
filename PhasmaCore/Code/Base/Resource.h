#pragma once

namespace pe
{
    class Resource
    {
    public:
        Resource() : m_resourceId(""), m_loaded(false) {}
        virtual ~Resource() = default;

        virtual void Load() {}
        virtual void Unload() {}

        const std::string &GetResourceId() const { return m_resourceId; }
        void SetResourceId(const std::string &id) { m_resourceId = id; }

        bool IsLoaded() const { return m_loaded; }

    protected:
        std::string m_resourceId;
        bool m_loaded;
    };
} // namespace pe
