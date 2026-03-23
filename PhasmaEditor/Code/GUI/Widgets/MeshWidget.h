#pragma once
#include "GUI/Widget.h"

namespace pe
{
    struct Mesh;
    struct NodeId;

    class MeshWidget : public Widget
    {
    public:
        MeshWidget();
        ~MeshWidget();

        void Update() override;
        void DrawEmbed(Mesh *mesh, NodeId *node);

    private:
        void DrawMaterialInfo(Mesh *mesh, NodeId *node);
        void DrawTextureInfo(Mesh *mesh, NodeId *node);
        void PropagateMeshChange(NodeId *node);
        void *GetDescriptor(Image *image);

        std::unordered_map<Image *, void *> m_textureDescriptors;
    };
} // namespace pe
