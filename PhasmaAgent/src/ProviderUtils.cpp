#include "PhasmaAgent/ProviderUtils.h"

#include <cstdio>
#include <cstdlib>

namespace pagent
{
    std::string GetEnvOrEmpty(const char *name)
    {
#if defined(_WIN32)
        char *value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, name) != 0 || !value)
            return {};

        std::string result(value);
        free(value);
        return result;
#else
        const char *value = std::getenv(name);
        return value ? value : "";
#endif
    }

} // namespace pagent
