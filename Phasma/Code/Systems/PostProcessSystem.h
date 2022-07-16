#pragma once

namespace pe
{
    class PostProcessSystem : public ISystem
    {
    public:
        PostProcessSystem();

        ~PostProcessSystem();

        void Init(CommandBuffer *cmd) override;

        void Update(double delta) override;

        void Destroy() override;

        void Resize(uint32_t width, uint32_t height);

        template <class T>
        T *GetEffect()
        {
            ValidateBaseClass<IRenderComponent, T>();
            return static_cast<T *>(m_effects[GetTypeID<T>()]);
        }

    private:
        std::unordered_map<size_t, IRenderComponent *> m_effects;
    };
}
