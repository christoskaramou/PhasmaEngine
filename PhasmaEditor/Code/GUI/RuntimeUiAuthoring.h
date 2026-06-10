#pragma once

#include "Scene/NodeComponents.h"
#include <array>

namespace pe
{
    class Scene;
    struct NodeId;

    namespace RuntimeUiAuthoring
    {
        constexpr const char *kDragPayloadType = "RUNTIME_UI_ELEMENT";

        struct DragPayload
        {
            NodeRuntimeUiWidgetType type = NodeRuntimeUiWidgetType::Panel;
        };

        struct ElementTemplate
        {
            NodeRuntimeUiWidgetType type = NodeRuntimeUiWidgetType::Panel;
            const char *name = "";
            const char *icon = "";
            const char *tooltip = "";
            vec2 size = vec2(240.0f, 120.0f);
        };

        const std::array<ElementTemplate, 4> &Templates();
        const ElementTemplate *FindTemplate(NodeRuntimeUiWidgetType type);
        const char *Label(NodeRuntimeUiWidgetType type);
        const char *Icon(NodeRuntimeUiWidgetType type);
        void ConfigureDefaults(NodeRuntimeUiTag &ui, NodeRuntimeUiWidgetType type);
        NodeId *CreateNode(Scene &scene, NodeRuntimeUiWidgetType type, const vec2 &topLeft, NodeId *parent = nullptr);
    } // namespace RuntimeUiAuthoring
} // namespace pe
