#pragma once

namespace pe
{
    class Context;

    class Surface : public IHandle<Surface, SurfaceHandle>
    {
    public:
        Surface(SDL_Window *window);

        ~Surface();

        void CheckTransfer();

        void FindFormat();

        void FindPresentationMode();

        void FindProperties();

        Rect2D actualExtent;
        Format format;
        ColorSpace colorSpace;
        PresentMode presentMode;
    };
}