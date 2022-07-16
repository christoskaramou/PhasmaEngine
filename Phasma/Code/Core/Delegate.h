#pragma once

namespace pe
{
    template <class... T>
    class Delegate
    {
    public:
        using Func_type = std::function<void(T &&...)>;

        inline void operator+=(Func_type &&func)
        {
            m_functions.push_back(std::forward<Func_type>(func));
        }

        inline void operator-=(Func_type &&func)
        {
            const size_t funcAddress = *reinterpret_cast<long *>(reinterpret_cast<char *>(&func));

            int index = 0;
            for (auto &function : m_functions)
            {
                const size_t functionAddress = *reinterpret_cast<long *>(reinterpret_cast<char *>(&function));

                // This equality seems to work, holding back though since it might not be accurate
                if (funcAddress == functionAddress)
                    m_functions.erase(m_functions.begin() + index);
                else
                    index++;
            }
        }

        inline void Invoke(T &&...args)
        {
            for (auto &function : m_functions)
                function(std::forward<T>(args)...);
        }

        inline void ReverseInvoke(T &&...args)
        {
            for (int i = static_cast<int>(m_functions.size()) - 1; i >= 0; i--)
                m_functions[i](std::forward<T>(args)...);
        }

        inline bool IsEmpty()
        {
            return m_functions.size() == 0;
        }

        inline void Clear()
        {
            m_functions.clear();
        }

    private:
        std::deque<Func_type> m_functions{};
    };
}