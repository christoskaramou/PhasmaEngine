/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    class Entity;

    class IComponent
    {
    public:
        IComponent() : m_entity(nullptr), m_enabled(false)
        {
        }

        virtual ~IComponent()
        {
        }

        Entity *GetEntity()
        {
            return m_entity;
        }

        void SetEntity(Entity *entity)
        {
            m_entity = entity;
        }

        bool IsEnabled()
        {
            return m_enabled;
        }

        void SetEnabled(bool enabled)
        {
            m_enabled = enabled;
        }

        virtual void Destroy()
        {
        }

    private:
        Entity *m_entity;
        bool m_enabled;
    };

    class Image;
    class CommandBuffer;
    class Camera;

    class IRenderComponent : public IComponent
    {  
    public:
        virtual void Init() = 0;

        virtual void CreateRenderPass() = 0;

        virtual void CreateFrameBuffers() = 0;

        virtual void CreatePipeline() = 0;

        virtual void CreateUniforms() = 0;

        virtual void UpdateDescriptorSets() = 0;

        virtual void Update(Camera *camera) = 0;

        virtual void Draw(CommandBuffer *cmd, uint32_t imageIndex) = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual void Destroy() = 0;
    };
}
