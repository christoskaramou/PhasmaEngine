#pragma once

namespace pe
{
    using WindowApiHandle = ApiHandle<ObjectType::Unknown, SDL_Window *, Placeholder<0> *>;
    class SplashScreen : public PeHandle<SplashScreen, WindowApiHandle>
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
