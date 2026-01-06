#include "GUI/Widget.h"
#include <unordered_map>


namespace pe
{
    class Particles : public Widget
    {
    public:
        Particles() : Widget("Particles") {}
        void Update() override;

    private:
        std::unordered_map<void *, void *> m_textureCache; // Image* -> VkDescriptorSet (stored as void*)
    };
} // namespace pe
