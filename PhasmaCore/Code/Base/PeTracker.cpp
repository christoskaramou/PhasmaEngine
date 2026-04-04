#include "Base/PeTracker.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace pe
{
    namespace
    {
        std::mutex s_globalMutex;
        std::unordered_map<size_t, std::deque<void *>> s_handlesMap;
    } // namespace

    void PeTracker::Track(const std::type_info &type, void *ptr)
    {
        std::lock_guard<std::mutex> lock(s_globalMutex);
        s_handlesMap[type.hash_code()].push_back(ptr);
    }

    void PeTracker::Untrack(const std::type_info &type, void *ptr)
    {
        std::lock_guard<std::mutex> lock(s_globalMutex);
        auto it = s_handlesMap.find(type.hash_code());
        if (it != s_handlesMap.end())
            std::erase(it->second, ptr);
    }

    std::vector<void *> PeTracker::GetHandles(const std::type_info &type)
    {
        std::lock_guard<std::mutex> lock(s_globalMutex);
        auto it = s_handlesMap.find(type.hash_code());
        if (it != s_handlesMap.end())
            return {it->second.begin(), it->second.end()};
        return {};
    }
} // namespace pe
