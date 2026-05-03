#pragma once

#include "API/Surface_Internal.h"

#if defined(PE_WIN32)

namespace pe
{
    struct Dx12SurfaceImpl final : public Surface::Impl
    {
        Dx12SurfaceImpl(Surface *owner, SDL_Window *window);
        ~Dx12SurfaceImpl() override = default;

        static Dx12SurfaceImpl *From(Surface *s) { return static_cast<Dx12SurfaceImpl *>(s->m_impl); }
        static const Dx12SurfaceImpl *From(const Surface *s) { return static_cast<const Dx12SurfaceImpl *>(s->m_impl); }
        static Dx12SurfaceImpl *TryFrom(Surface *s) { return s && s->m_impl ? From(s) : nullptr; }
        static const Dx12SurfaceImpl *TryFrom(const Surface *s) { return s && s->m_impl ? From(s) : nullptr; }

        std::vector<PePresentMode> GetSupportedPresentModes() const override;

        HWND GetHwnd() const { return m_hwnd; }

        Surface *m_owner{};
        HWND m_hwnd{};
    };
} // namespace pe

#endif // PE_WIN32
