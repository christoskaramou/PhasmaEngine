#pragma once

namespace pe
{
    class SplashScreen : public PeHandle<SplashScreen, SDL_Window *>
    {
    public:
        SplashScreen(uint32_t flags);
        ~SplashScreen();

    private:
        SDL_Surface *m_surface;
        SDL_Renderer *m_renderer;
        SDL_Texture *m_texture;
    };
} // namespace pe
