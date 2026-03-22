#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class ModelAsset;
    class MeshInfo;

    class MeshWidget : public Widget
    {
    public:
        MeshWidget();
        ~MeshWidget();

        void Update() override;
        void DrawEmbed(MeshInfo *mesh, ModelAsset *model);

    private:
        void DrawMaterialInfo(MeshInfo *mesh, ModelAsset *model);
        void DrawTextureInfo(MeshInfo *mesh, ModelAsset *model);
        void PropagateMeshChange(MeshInfo *mesh, ModelAsset *model);
        void *GetDescriptor(Image *image);

        std::unordered_map<Image *, void *> m_textureDescriptors;
    };
} // namespace pe
