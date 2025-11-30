#pragma once

namespace pe
{
    using SceneNodeHandle = uint32_t;
    constexpr SceneNodeHandle InvalidSceneNode = std::numeric_limits<SceneNodeHandle>::max();

    // Basic TRS description shared by graph nodes.
    struct SceneTransform
    {
        SceneTransform();
        SceneTransform(const vec3 &translation, const quat &rotation, const vec3 &scale);

        vec3 translation;
        quat rotation;
        vec3 scale;

        [[nodiscard]] mat4 ToMatrix() const;
    };

    // Contiguous node representation with intrusive siblings for cache-friendly traversal.
    struct SceneNode
    {
        SceneNodeHandle id;
        SceneNodeHandle parent;
        SceneNodeHandle firstChild;
        SceneNodeHandle nextSibling;
        SceneNodeHandle prevSibling;
        mat4 world;
        SceneTransform local;
        bool dirty;
    };

    // Light-weight hierarchy with lazy world-matrix evaluation via dirty flags.
    class SceneGraph
    {
    public:
        SceneGraph();

        SceneNodeHandle CreateNode(SceneNodeHandle parent = InvalidSceneNode);
        void DestroyNode(SceneNodeHandle node);

        SceneNode *GetNode(SceneNodeHandle node);
        const SceneNode *GetNode(SceneNodeHandle node) const;

        void SetParent(SceneNodeHandle node, SceneNodeHandle newParent);
        void SetLocalTransform(SceneNodeHandle node, const SceneTransform &transform);
        void SetLocalTranslation(SceneNodeHandle node, const vec3 &translation);
        void SetLocalRotation(SceneNodeHandle node, const quat &rotation);
        void SetLocalScale(SceneNodeHandle node, const vec3 &scale);
        const SceneTransform &GetLocalTransform(SceneNodeHandle node) const;
        const mat4 &GetWorldMatrix(SceneNodeHandle node);

        void MarkDirty(SceneNodeHandle node);
        void UpdateWorldTransforms();
        const std::vector<SceneNode> &GetNodes() const noexcept { return m_nodes; }

    private:
        [[nodiscard]] bool IsValid(SceneNodeHandle node) const;
        SceneNodeHandle AllocateNode();
        void LinkChild(SceneNodeHandle child, SceneNodeHandle parent);
        void UnlinkChild(SceneNodeHandle child);
        void UpdateNodeRecursive(SceneNodeHandle node, const mat4 &parentWorld, bool parentDirty);

        std::vector<SceneNode> m_nodes;
        std::vector<SceneNodeHandle> m_freeList;
        bool m_globalDirty;
    };
}
