#include "Base/Settings.h"

namespace pe
{
    // Explicit instantiation — ensures a single Settings::Get<GlobalSettings>()
    // definition exists in PhasmaCore.dll. Without this, each module (DLL + EXE)
    // gets its own function-local static 'value', splitting the settings singleton.
    template PE_API GlobalSettings &Settings::Get<GlobalSettings>();
} // namespace pe
