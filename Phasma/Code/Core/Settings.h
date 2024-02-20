#pragma once

namespace pe
{
    constexpr uint32_t SWAPCHAIN_IMAGES = 3;

    class Setting
    {
    };

    class Settings
    {
    public:
        template <class T>
        static T &Get()
        {
            ValidateBaseClass<Setting, T>();
            static T value{};
            return value;
        }
    };

    struct Global : public Setting
    {
        bool rightHanded = false;
        bool reverseZ = true;
        bool frustumCulling = true;
        uint32_t shadowMapSize = 2048;
        uint32_t shadowMapCascades = 4;
    };
}
