#pragma once

#include "GUI/Widget.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <vector>

namespace pe
{
    class PrefabViewer : public Widget
    {
    public:
        PrefabViewer();
        void Update() override;

        bool OpenPrefab(const std::filesystem::path &path);

    private:
        struct PrefabNodeView
        {
            std::string name;
            int parent = -1;
            bool enabled = true;
            std::vector<int> children;
        };

        bool LoadFromDisk(const std::filesystem::path &path);
        bool SaveToDisk();
        void OpenFileDialog();
        void ReloadFromDisk();
        void RebuildNodeViews();
        void DrawToolbar();
        void DrawTreePanel();
        void DrawNodeTree(int nodeIndex);
        void DrawInspectorPanel();
        void DrawEmptyState();
        void DrawTransformEditor(int nodeIndex);
        void DrawMeshEditor(int nodeIndex);
        void DrawComponentEditors(int nodeIndex);
        void DrawAddMenu(int parentIndex);
        void DrawAttachMeshMenu(int nodeIndex);
        void DrawComponentAddMenu(int nodeIndex);
        void DrawJsonSummary(int nodeIndex);
        void AddChild(int parentIndex);
        int AddNode(int parentIndex, const std::string &name);
        void AddPrimitiveMeshChild(int parentIndex, const std::string &primitiveType, const std::string &name);
        void AttachPrimitiveMesh(int nodeIndex, const std::string &primitiveType);
        void OpenCookedMeshDialog(bool asChild, int targetIndex);
        void AttachCookedMesh(int nodeIndex, const std::filesystem::path &path);
        void OpenScriptDialog(int nodeIndex);
        void RemoveSelectedNode();
        void ReparentNode(int nodeIndex, int newParent);
        void InstantiateIntoScene();
        int AddPrimitiveSource(const std::string &primitiveType);
        int AddCookedMeshSource(const std::filesystem::path &path);
        int AddMeshForSource(int sourceIndex, int sourceMeshIndex = 0);
        void AttachMeshRef(int nodeIndex, int meshIndex);
        void AddComponent(int nodeIndex, uint32_t componentFlag, const std::string &componentKind = {});
        void RemoveComponent(int nodeIndex, uint32_t componentFlag);
        uint32_t GetNodeFlags(const nlohmann::json &nodeJson) const;
        void SetNodeFlags(nlohmann::json &nodeJson, uint32_t flags);
        std::string SerializePrefabRelativePath(const std::filesystem::path &path) const;
        bool CanReparent(int nodeIndex, int newParent) const;
        bool IsDescendantOf(int nodeIndex, int possibleAncestor) const;
        std::string MakeUniqueChildName(int parentIndex, const std::string &baseName) const;
        void MarkDirty();

        std::filesystem::path m_prefabPath;
        nlohmann::json m_document;
        std::vector<PrefabNodeView> m_nodes;
        int m_rootIndex = 0;
        int m_selectedIndex = -1;
        bool m_dirty = false;
        std::string m_status;
        char m_renameBuffer[256] = {};
    };
} // namespace pe
