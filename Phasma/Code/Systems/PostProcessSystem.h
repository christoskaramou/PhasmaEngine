#pragma once

namespace pe
{
    class PostProcessSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *cmd) override;
        void Update(double delta) override;
        void Destroy() override;
    
        void Resize(uint32_t width, uint32_t height);
        
        template <class T>
        T *GetEffect()
        {
            ValidateBaseClass<IRenderPassComponent, T>();
            return static_cast<T *>(m_renderPassComponents[ID::GetTypeID<T>()]);
        }

    private:
        std::unordered_map<size_t, IRenderPassComponent *> m_renderPassComponents;
    };
}
