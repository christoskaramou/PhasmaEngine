#include "GUI/Widget.h"

namespace pe
{
    class Particles : public Widget
    {
    public:
        Particles() : Widget("Particles") { m_open = false; }
        void Update() override;

    private:
        std::unordered_map<void *, void *> m_textureCache; // Image* -> VkDescriptorSet (stored as void*)
    };
} // namespace pe
