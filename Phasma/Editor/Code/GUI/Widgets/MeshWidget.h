#pragma once
#include "GUI/Widget.h"
#include "GUI/Widgets/MaterialEditorWidget.h"
#include "Scene/SceneNodeHandle.h"

namespace pe
{
    struct Mesh;
    struct NodeId;
    class MaterialInstance;

    class MeshWidget : public Widget
    {
    public:
        MeshWidget();
        ~MeshWidget();

        void Update() override;
        void DrawEmbed(Mesh *mesh, NodeId *node);

    private:
        struct OpenPBREditState
        {
            char document[2048]{};
            char material[128] = "Material";
            std::string error;
            std::shared_ptr<const OpenPBRMaterialSource> source;
        };

        void DrawMaterialInfo(Mesh *mesh, NodeId *node);
        bool DrawOpenPBRMaterial(Mesh &mesh);
        void DrawOpenPBRAssignment(Mesh &mesh, OpenPBREditState &state);
        void DrawOpenPBRInputs(MaterialInstance &instance, std::string &error);
        void DrawTextureInfo(Mesh *mesh, NodeId *node);
        void PropagateMeshChange(NodeId *node);
        void *GetDescriptor(Image *image);

        std::unordered_map<Image *, void *> m_textureDescriptors;
        MaterialEditorWidget m_materialEditor;
        SceneNodeHandle m_openpbrEditNode;
        std::unordered_map<Mesh *, OpenPBREditState> m_openpbrEdits;
    };
} // namespace pe
