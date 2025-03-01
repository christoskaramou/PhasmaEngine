#pragma once

#include "Renderer/Renderer.h"
#include "Skybox/Skybox.h"
#include "GUI/GUI.h"
#include "Script/Script.h"
#include "Scene/Scene.h"

namespace pe
{
    class CommandBuffer;

    class RendererSystem : public Renderer, public IDrawSystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;
        void Draw() override;

        Scene &GetScene() { return m_scene; }
        const SkyBox &GetSkyBoxDay() const { return m_skyBoxDay; }
        const SkyBox &GetSkyBoxNight() const { return m_skyBoxNight; }
        const GUI &GetGUI() const { return m_gui; }
        void ToggleGUI() { m_gui.ToggleRender(); }

    private:
        void WaitPreviousFrameCommands();
        void LoadResources(CommandBuffer *cmd);
        CommandBuffer *RecordPasses(uint32_t imageIndex);

        Scene m_scene;
        SkyBox m_skyBoxDay;
        SkyBox m_skyBoxNight;
        GUI m_gui;
    };
}
