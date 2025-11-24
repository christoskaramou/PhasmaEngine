#pragma once

#include "Scene/Geometry.h"

namespace pe
{
    class CommandBuffer;
    class Model;

    class Scene
    {
    public:
        Scene();
        ~Scene();

        void Update();
        void UpdateGeometryBuffers(CommandBuffer *cmd);
        void DrawShadowPass(CommandBuffer *cmd);
        void DepthPrePass(CommandBuffer *cmd);
        void DrawGbufferPassOpaque(CommandBuffer *cmd);
        void DrawGbufferPassTransparent(CommandBuffer *cmd);
        void DrawLightPassOpaque(CommandBuffer *cmd);
        void DrawLightPassTransparent(CommandBuffer *cmd);
        void DrawAabbsPass(CommandBuffer *cmd);
        void DrawScene(CommandBuffer *cmd);
        void AddModel(Model *model);
        void RemoveModel(Model *model);

    private:
        Geometry m_geometry{};
    };
}
