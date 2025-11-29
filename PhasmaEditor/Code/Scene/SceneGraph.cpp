#include "Scene/SceneGraph.h"

namespace pe
{
    namespace
    {
        inline quat IdentityQuaternion()
        {
            return quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        inline mat4 IdentityMatrix()
        {
            return mat4(1.0f);
        }

        inline mat4 ComposeMatrix(const SceneTransform &sceneTransform)
        {
            const mat4 translationMat = glm::translate(mat4(1.0f), sceneTransform.translation);
            const mat4 rotationMat = glm::mat4_cast(sceneTransform.rotation);
            const mat4 scaleMat = glm::scale(mat4(1.0f), sceneTransform.scale);
            return translationMat * rotationMat * scaleMat;
        }
    }

    SceneTransform::SceneTransform()
        : translation(0.0f), rotation(IdentityQuaternion()), scale(1.0f)
    {
    }

    SceneTransform::SceneTransform(const vec3 &translationValue, const quat &rotationValue, const vec3 &scaleValue)
        : translation(translationValue), rotation(rotationValue), scale(scaleValue)
    {
    }

    mat4 SceneTransform::ToMatrix() const
    {
        return ComposeMatrix(*this);
    }

    SceneGraph::SceneGraph()
        : m_nodes(), m_freeList(), m_globalDirty(false)
    {
    }

    SceneNodeHandle SceneGraph::CreateNode(SceneNodeHandle parent)
    {
        SceneNodeHandle handle = AllocateNode();
        SceneNode &node = m_nodes[handle];
        node.id = handle;
        node.parent = InvalidSceneNode;
        node.firstChild = InvalidSceneNode;
        node.nextSibling = InvalidSceneNode;
        node.prevSibling = InvalidSceneNode;
        node.world = IdentityMatrix();
        node.local = SceneTransform();
        node.dirty = true;

        if (IsValid(parent))
            LinkChild(handle, parent);

        m_globalDirty = true;
        return handle;
    }

    void SceneGraph::DestroyNode(SceneNodeHandle node)
    {
        if (!IsValid(node))
            return;

        SceneNode &current = m_nodes[node];
        SceneNodeHandle child = current.firstChild;
        while (IsValid(child))
        {
            SceneNodeHandle next = m_nodes[child].nextSibling;
            DestroyNode(child);
            child = next;
        }

        UnlinkChild(node);
        current.id = InvalidSceneNode;
        current.parent = InvalidSceneNode;
        current.firstChild = InvalidSceneNode;
        current.nextSibling = InvalidSceneNode;
        current.prevSibling = InvalidSceneNode;
        m_freeList.push_back(node);
        m_globalDirty = true;
    }

    SceneNode *SceneGraph::GetNode(SceneNodeHandle node)
    {
        if (!IsValid(node))
            return nullptr;
        return &m_nodes[node];
    }

    const SceneNode *SceneGraph::GetNode(SceneNodeHandle node) const
    {
        if (!IsValid(node))
            return nullptr;
        return &m_nodes[node];
    }

    void SceneGraph::SetParent(SceneNodeHandle node, SceneNodeHandle newParent)
    {
        if (!IsValid(node))
            return;

        if (node == newParent)
            return;

        UnlinkChild(node);

        if (IsValid(newParent))
            LinkChild(node, newParent);

        MarkDirty(node);
    }

    void SceneGraph::SetLocalTransform(SceneNodeHandle node, const SceneTransform &transform)
    {
        if (!IsValid(node))
            return;

        m_nodes[node].local = transform;
        MarkDirty(node);
    }

    void SceneGraph::SetLocalTranslation(SceneNodeHandle node, const vec3 &translation)
    {
        if (!IsValid(node))
            return;

        m_nodes[node].local.translation = translation;
        MarkDirty(node);
    }

    void SceneGraph::SetLocalRotation(SceneNodeHandle node, const quat &rotation)
    {
        if (!IsValid(node))
            return;

        m_nodes[node].local.rotation = rotation;
        MarkDirty(node);
    }

    void SceneGraph::SetLocalScale(SceneNodeHandle node, const vec3 &scale)
    {
        if (!IsValid(node))
            return;

        m_nodes[node].local.scale = scale;
        MarkDirty(node);
    }

    const SceneTransform &SceneGraph::GetLocalTransform(SceneNodeHandle node) const
    {
        static SceneTransform s_identity{};
        if (!IsValid(node))
            return s_identity;
        return m_nodes[node].local;
    }

    const mat4 &SceneGraph::GetWorldMatrix(SceneNodeHandle node)
    {
        static mat4 s_identity = IdentityMatrix();
        if (!IsValid(node))
            return s_identity;

        if (m_globalDirty)
            UpdateWorldTransforms();

        return m_nodes[node].world;
    }

    void SceneGraph::MarkDirty(SceneNodeHandle node)
    {
        if (!IsValid(node))
            return;

        std::vector<SceneNodeHandle> stack;
        stack.push_back(node);

        while (!stack.empty())
        {
            SceneNodeHandle current = stack.back();
            stack.pop_back();

            if (!IsValid(current))
                continue;

            SceneNode &currentNode = m_nodes[current];
            if (!currentNode.dirty)
                currentNode.dirty = true;

            SceneNodeHandle child = currentNode.firstChild;
            while (IsValid(child))
            {
                stack.push_back(child);
                child = m_nodes[child].nextSibling;
            }
        }

        m_globalDirty = true;
    }

    void SceneGraph::UpdateWorldTransforms()
    {
        if (!m_globalDirty)
            return;

        const mat4 identity = IdentityMatrix();
        for (SceneNodeHandle i = 0; i < m_nodes.size(); ++i)
        {
            if (!IsValid(i))
                continue;

            SceneNode &node = m_nodes[i];
            if (node.parent != InvalidSceneNode)
                continue;

            UpdateNodeRecursive(node.id, identity, true);
        }

        m_globalDirty = false;
    }

    bool SceneGraph::IsValid(SceneNodeHandle node) const
    {
        return node != InvalidSceneNode && node < m_nodes.size() && m_nodes[node].id != InvalidSceneNode;
    }

    SceneNodeHandle SceneGraph::AllocateNode()
    {
        if (!m_freeList.empty())
        {
            SceneNodeHandle handle = m_freeList.back();
            m_freeList.pop_back();
            return handle;
        }

        SceneNodeHandle handle = static_cast<SceneNodeHandle>(m_nodes.size());
        m_nodes.emplace_back();
        return handle;
    }

    void SceneGraph::LinkChild(SceneNodeHandle child, SceneNodeHandle parent)
    {
        if (!IsValid(child))
            return;

        SceneNode &childNode = m_nodes[child];
        SceneNode &parentNode = m_nodes[parent];

        childNode.parent = parent;
        childNode.nextSibling = parentNode.firstChild;
        childNode.prevSibling = InvalidSceneNode;

        if (IsValid(parentNode.firstChild))
            m_nodes[parentNode.firstChild].prevSibling = child;

        parentNode.firstChild = child;
    }

    void SceneGraph::UnlinkChild(SceneNodeHandle child)
    {
        if (!IsValid(child))
            return;

        SceneNode &childNode = m_nodes[child];
        if (!IsValid(childNode.parent))
            return;

        SceneNode &parentNode = m_nodes[childNode.parent];
        if (parentNode.firstChild == child)
            parentNode.firstChild = childNode.nextSibling;

        if (IsValid(childNode.prevSibling))
            m_nodes[childNode.prevSibling].nextSibling = childNode.nextSibling;

        if (IsValid(childNode.nextSibling))
            m_nodes[childNode.nextSibling].prevSibling = childNode.prevSibling;

        childNode.parent = InvalidSceneNode;
        childNode.nextSibling = InvalidSceneNode;
        childNode.prevSibling = InvalidSceneNode;
    }

    void SceneGraph::UpdateNodeRecursive(SceneNodeHandle nodeHandle, const mat4 &parentWorld, bool parentDirty)
    {
        if (!IsValid(nodeHandle))
            return;

        SceneNode &node = m_nodes[nodeHandle];
        bool needsUpdate = parentDirty || node.dirty;
        if (needsUpdate)
        {
            node.world = parentWorld * node.local.ToMatrix();
            node.dirty = false;
        }

        SceneNodeHandle child = node.firstChild;
        while (IsValid(child))
        {
            UpdateNodeRecursive(child, node.world, needsUpdate);
            child = m_nodes[child].nextSibling;
        }
    }
}
