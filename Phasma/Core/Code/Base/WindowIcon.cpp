#include "Base/WindowIcon.h"
#include "Base/FileSystem.h"
#include "Base/Log.h"
#include "Base/Path.h"

namespace pe
{
    void SetPhasmaWindowIcon(SDL_Window *window)
    {
        if (!window)
            return;

        // FileSystem serves the game pack, where an exported game keeps its runtime assets.
        const std::string iconPath = Path::RuntimeAssetsPath() + "Icons/phasma_icon.bmp";
        FileSystem file(iconPath, std::ios_base::in);
        const std::vector<uint8_t> bytes = file.IsOpen() ? file.ReadAllBytes() : std::vector<uint8_t>{};
        SDL_Surface *icon = SDL_LoadBMP_RW(SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size())), 1);
        if (!icon)
        {
            Log::Warn("Failed to load the Phasma window icon '" + iconPath + "': " + SDL_GetError());
            return;
        }

        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
} // namespace pe
