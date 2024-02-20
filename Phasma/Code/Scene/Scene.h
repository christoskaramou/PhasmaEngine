#pragma once

#include "Scene/Geometry.h"

namespace pe
{
    class CommandBuffer;
    class ModelGltf;
    class Queue;

    class Scene
    {
    public:
        Scene();

        ~Scene();

        void Update(double delta);

        void InitGeometry(CommandBuffer *cmd);

        CommandBuffer *DrawShadowPass();

        CommandBuffer *DepthPrePass();

        CommandBuffer *DrawGbufferPassOpaque();

        CommandBuffer *DrawGbufferPassTransparent();

        CommandBuffer *DrawLightPassOpaque();

        CommandBuffer *DrawLightPassTransparent();

        CommandBuffer *DrawAabbsPass();

        void DrawScene();

        void AddModel(ModelGltf *model);

        void RemoveModel(ModelGltf *model);

    private:
        Geometry m_geometry{};
        Queue *m_renderQueue{};
    };
}
