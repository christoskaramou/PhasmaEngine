#pragma once

namespace pe
{
    template <class... Args>
    class Delegate
    {
    public:
        using FunctionType = std::function<void(Args...)>;

        // Adds a function to the delegate
        inline void Add(FunctionType &&func)
        {
            m_functions.push_back(std::forward<FunctionType>(func));
        }

        // Adds a function to the delegate
        inline void operator+=(FunctionType &&func)
        {
            m_functions.push_back(std::forward<FunctionType>(func));
        }

        // Invokes all functions stored in the delegate
        inline void Invoke(Args &&...args) const
        {
            for (const auto &function : m_functions)
            {
                function(std::forward<Args>(args)...);
            }
        }

        // Invokes all functions stored in the delegate in reverse order
        inline void ReverseInvoke(Args &&...args) const
        {
            for (size_t i = m_functions.size(); i-- > 0;)
            {
                m_functions[i](std::forward<Args>(args)...);
            }
        }

        // Checks if the delegate is empty
        inline bool IsEmpty() const
        {
            return m_functions.empty();
        }

        // Clears all functions from the delegate
        inline void Clear()
        {
            m_functions.clear();
        }

    private:
        std::vector<FunctionType> m_functions;
    };
}