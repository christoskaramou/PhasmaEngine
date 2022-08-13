#pragma once

namespace pe
{
    class SplashScreen : public IHandle<SplashScreen, WindowHandle>
    {
    public:
        SplashScreen(uint32_t flags);

        ~SplashScreen();

    private:
        SDL_Surface *m_surface;
        SDL_Renderer *m_renderer;
        SDL_Texture *m_texture;
    };
}