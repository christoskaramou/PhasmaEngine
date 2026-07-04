#include "RuntimeUiAuthoring.h"
#include "GUI/IconsFontAwesome.h"
#include "Scene/Scene.h"

namespace pe::RuntimeUiAuthoring
{
    const std::array<ElementTemplate, 4> &Templates()
    {
        // Sorted by display name — drives the "Add -> UI" submenu order everywhere. FindTemplate matches
        // by type (not index), so reordering is safe.
        static const std::array<ElementTemplate, 4> templates = {
            ElementTemplate{NodeRuntimeUiWidgetType::Button,
                            "Button",
                            ICON_FA_VECTOR_SQUARE,
                            "Create an authored Runtime UI button node.",
                            vec2(168.0f, 56.0f)},
            ElementTemplate{NodeRuntimeUiWidgetType::Image,
                            "Image",
                            ICON_FA_IMAGE,
                            "Create an authored Runtime UI image node.",
                            vec2(240.0f, 160.0f)},
            ElementTemplate{NodeRuntimeUiWidgetType::Panel,
                            "Panel",
                            ICON_FA_WINDOW_MAXIMIZE,
                            "Create an authored Runtime UI panel node.",
                            vec2(320.0f, 180.0f)},
            ElementTemplate{NodeRuntimeUiWidgetType::Text,
                            "Text",
                            ICON_FA_FILE,
                            "Create an authored Runtime UI text node.",
                            vec2(220.0f, 64.0f)},
        };
        return templates;
    }

    const ElementTemplate *FindTemplate(NodeRuntimeUiWidgetType type)
    {
        for (const ElementTemplate &element : Templates())
            if (element.type == type)
                return &element;
        return &Templates().front();
    }

    const char *Label(NodeRuntimeUiWidgetType type)
    {
        const ElementTemplate *element = FindTemplate(type);
        return element ? element->name : "UI";
    }

    const char *Icon(NodeRuntimeUiWidgetType type)
    {
        const ElementTemplate *element = FindTemplate(type);
        return element ? element->icon : ICON_FA_WINDOW_MAXIMIZE;
    }

    void ConfigureDefaults(NodeRuntimeUiTag &ui, NodeRuntimeUiWidgetType type)
    {
        ui = NodeRuntimeUiTag{};
        ui.authored = true;
        ui.widgetType = type;
        ui.screenId = "__scene_ui";
        ui.visible = true;

        switch (type)
        {
        case NodeRuntimeUiWidgetType::Text:
            ui.body = "Text";
            ui.fillColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
            ui.borderColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
            ui.accentColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
            ui.textColor = vec4(0.96f, 0.97f, 0.98f, 1.0f);
            break;
        case NodeRuntimeUiWidgetType::Button:
            ui.title = "Button";
            ui.actionFunction = "on_click";
            ui.fillColor = vec4(0.12f, 0.14f, 0.17f, 0.96f);
            ui.borderColor = vec4(0.96f, 0.74f, 0.22f, 0.95f);
            ui.accentColor = vec4(0.96f, 0.74f, 0.22f, 1.0f);
            ui.textColor = vec4(0.97f, 0.98f, 1.0f, 1.0f);
            break;
        case NodeRuntimeUiWidgetType::Image:
            ui.fillColor = vec4(0.05f, 0.06f, 0.07f, 0.86f);
            ui.borderColor = vec4(0.45f, 0.48f, 0.54f, 0.95f);
            ui.accentColor = vec4(0.18f, 0.22f, 0.28f, 0.85f);
            ui.textColor = vec4(0.92f, 0.93f, 0.94f, 1.0f);
            break;
        case NodeRuntimeUiWidgetType::Panel:
        default:
            ui.label = "UI";
            ui.title = "Panel";
            ui.body = "Runtime UI panel";
            ui.fillColor = vec4(0.07f, 0.08f, 0.10f, 0.94f);
            ui.borderColor = vec4(0.45f, 0.48f, 0.54f, 0.95f);
            ui.accentColor = vec4(0.96f, 0.74f, 0.22f, 1.0f);
            ui.textColor = vec4(0.92f, 0.93f, 0.94f, 1.0f);
            break;
        }
    }

    NodeId *CreateNode(Scene &scene, NodeRuntimeUiWidgetType type, const vec2 &topLeft, NodeId *parent)
    {
        const ElementTemplate *element = FindTemplate(type);
        const char *label = element ? element->name : "UI";
        const vec2 size = element ? element->size : vec2(240.0f, 120.0f);

        NodeId *node = scene.CreateNode(std::string("UI ") + label, parent);
        NodeRuntimeUiTag &ui = scene.GetOrCreateRuntimeUiComponent(node);
        ConfigureDefaults(ui, type);

        const mat4 world =
            glm::translate(mat4(1.0f), vec3(topLeft.x, topLeft.y, 0.0f)) *
            glm::scale(mat4(1.0f), vec3(size.x, size.y, 1.0f));
        mat4 local = world;
        if (parent && scene.IsNodeAlive(parent))
        {
            const mat4 &parentWorld = scene.GetWorldMatrix(parent);
            const float det = glm::determinant(parentWorld);
            if (std::isfinite(det) && std::abs(det) > 1e-6f)
                local = glm::inverse(parentWorld) * world;
        }
        scene.SetLocalMatrix(node, local);
        scene.MarkDirty();
        return node;
    }
} // namespace pe::RuntimeUiAuthoring
