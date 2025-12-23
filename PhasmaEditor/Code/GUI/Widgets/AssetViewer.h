#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class AssetViewer : public Widget
    {
    public:
        AssetViewer() : Widget("Asset Viewer") {}
        void Update() override;
    };
} // namespace pe
