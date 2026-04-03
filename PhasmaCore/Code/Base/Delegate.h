#pragma once

namespace pe
{
    template <class... Args>
    class Delegate
    {
    public:
        using FunctionType = std::function<void(Args...)>;
        using Token = uint64_t;

        inline void Add(FunctionType &&func)
        {
            m_functions.push_back(std::forward<FunctionType>(func));
            m_tokens.push_back(0);
        }

        inline void operator+=(FunctionType &&func)
        {
            m_functions.push_back(std::forward<FunctionType>(func));
            m_tokens.push_back(0);
        }

        inline Token AddWithToken(FunctionType &&func)
        {
            Token t = m_nextToken++;
            m_tokens.push_back(t);
            m_functions.push_back(std::forward<FunctionType>(func));
            return t;
        }

        inline bool Remove(Token t)
        {
            if (t == 0)
                return false;
            for (size_t i = 0; i < m_tokens.size(); ++i)
            {
                if (m_tokens[i] == t)
                {
                    m_functions.erase(m_functions.begin() + i);
                    m_tokens.erase(m_tokens.begin() + i);
                    return true;
                }
            }
            return false;
        }

        inline void Invoke(Args &&...args) const
        {
            for (const auto &function : m_functions)
            {
                function(std::forward<Args>(args)...);
            }
        }

        inline void ReverseInvoke(Args &&...args) const
        {
            for (size_t i = m_functions.size(); i-- > 0;)
            {
                m_functions[i](std::forward<Args>(args)...);
            }
        }

        inline bool IsEmpty() const
        {
            return m_functions.empty();
        }

        inline void Clear()
        {
            m_functions.clear();
            m_tokens.clear();
        }

    private:
        std::vector<FunctionType> m_functions;
        std::vector<Token> m_tokens;
        Token m_nextToken{1};
    };
} // namespace pe