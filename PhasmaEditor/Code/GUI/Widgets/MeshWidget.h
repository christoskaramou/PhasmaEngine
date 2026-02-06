#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Model;
    class MeshInfo;

    class MeshWidget : public Widget
    {
    public:
        MeshWidget();
        ~MeshWidget();

        void Update() override;
        void DrawEmbed(MeshInfo *mesh, Model *model);

    private:
        void DrawMaterialInfo(MeshInfo *mesh, Model *model);
        void DrawTextureInfo(MeshInfo *mesh, Model *model);
        void PropagateMeshChange(MeshInfo *mesh, Model *model);
        void *GetDescriptor(Image *image);

        std::unordered_map<Image *, void *> m_textureDescriptors;
    };
} // namespace pe
