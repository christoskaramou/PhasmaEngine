#pragma once

#include "Scene/Geometry.h"

namespace pe
{
    class CommandBuffer;
    class ModelGltf;

    class Scene
    {
    public:
        Scene();
        ~Scene();

        void Update(double delta);
        void InitGeometry(CommandBuffer *cmd);
        void DrawShadowPass(CommandBuffer *cmd);
        void DepthPrePass(CommandBuffer *cmd);
        void DrawGbufferPassOpaque(CommandBuffer *cmd);
        void DrawGbufferPassTransparent(CommandBuffer *cmd);
        void DrawLightPassOpaque(CommandBuffer *cmd);
        void DrawLightPassTransparent(CommandBuffer *cmd);
        void DrawAabbsPass(CommandBuffer *cmd);
        void DrawScene(CommandBuffer *cmd);
        void AddModel(ModelGltf *model);
        void RemoveModel(ModelGltf *model);

    private:
        Geometry m_geometry{};
    };
}
