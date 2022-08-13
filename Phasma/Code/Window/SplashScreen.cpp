#include "Window/SplashScreen.h"
#include "tinygltf/stb_image.h"

namespace pe
{
    SplashScreen::SplashScreen(uint32_t flags)
    {
        if (!SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
            {
                std::cout << SDL_GetError();
                return;
            }
        }

        const char *surface_path = "C:/Users/chris/source/repos/PhasmaEngine/Phasma/Images/splash_screen.jpg";
        int width, height, nrChannels;
        unsigned char *data = stbi_load(surface_path, &width, &height, &nrChannels, STBI_rgb_alpha);
        m_surface = SDL_CreateRGBSurfaceFrom(data, width, height, 32, width * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
        if (!m_surface)
        {
            SDL_Log("Failed to load image at %s: %s\n", surface_path, SDL_GetError());
            return;
        }

        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
        {
            SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
            return;
        }

        int x = dm.w / 4;
        int y = dm.h / 4;
        int w = dm.w / 2;
        int h = dm.h / 2;

        m_handle = SDL_CreateWindow("", x, y, w, h, flags);
        m_renderer = SDL_CreateRenderer(m_handle, -1, 0);
        m_texture = SDL_CreateTextureFromSurface(m_renderer, m_surface);
        SDL_FreeSurface(m_surface);

        SDL_SetWindowAlwaysOnTop(m_handle, SDL_TRUE);

        SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
    }

    SplashScreen::~SplashScreen()
    {
        SDL_DestroyWindow(m_handle);
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroyTexture(m_texture);
    }
}
