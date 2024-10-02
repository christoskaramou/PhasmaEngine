#pragma once

namespace pe
{
    class Context;

    class Surface : public PeHandle<Surface, SurfaceApiHandle>
    {
    public:
        Surface(SDL_Window *window);
        ~Surface();

        void CheckTransfer();
        void FindFormat();
        void FindPresentationMode();
        void FindProperties();
        const Rect2Du &GetActualExtent() const { return m_actualExtent; }
        void SetActualExtent(const Rect2Du &extent) { m_actualExtent = extent; }
        Format GetFormat() const { return m_format; }
        ColorSpace GetColorSpace() const { return m_colorSpace; }
        PresentMode GetPresentMode() const { return m_presentMode; }

private:
        Rect2Du m_actualExtent;
        Format m_format;
        ColorSpace m_colorSpace;
        PresentMode m_presentMode;
    };
}