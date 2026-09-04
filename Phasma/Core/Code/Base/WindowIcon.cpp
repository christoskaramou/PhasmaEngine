#include "Base/WindowIcon.h"
#include "Base/Log.h"
#include "Base/Path.h"

#include <SDL.h>

namespace pe
{
    void SetPhasmaWindowIcon(SDL_Window *window)
    {
        if (!window)
            return;

        const std::string iconPath = Path::RuntimeAssetsPath() + "Icons/phasma_icon.bmp";
        SDL_Surface *icon = SDL_LoadBMP(iconPath.c_str());
        if (!icon)
        {
            Log::Warn("Failed to load the Phasma window icon '" + iconPath + "': " + SDL_GetError());
            return;
        }

        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
} // namespace pe
