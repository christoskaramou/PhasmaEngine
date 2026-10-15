#pragma once

#include <string>

namespace phasma::driver
{
    // Credential Manager Web Credential resource "jev", user "api-key", read in-process (no child process).
    std::string ReadSavedJevKey();
    bool HasSavedJevKey();
    void SaveJevKey(const std::string &key);
    bool RemoveJevKey();
} // namespace phasma::driver
