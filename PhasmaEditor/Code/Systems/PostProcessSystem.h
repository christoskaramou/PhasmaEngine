#pragma once

namespace pe
{
    class PostProcessSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        void Resize(uint32_t width, uint32_t height);
        void PollShaders();

        template <class T>
        T *GetEffect()
        {
            ValidateBaseClass<IRenderPassComponent, T>();
            return static_cast<T *>(m_renderPassComponents[ID::GetTypeID<T>()]);
        }

    private:
        OrderedMap<size_t, IRenderPassComponent *> m_renderPassComponents;
    };
} // namespace pe
