#include "ScriptModule.h"
#include <cmath>
namespace
{
    class PlayerController
    {
    public:
        PlayerController(const phasma::ScriptApi &api, phasma::Node node) : world(api), node(node) {}

        void Update(double dt)
        {
            if (!std::isfinite(dt) || dt <= 0.0)
                return;
            phasma::Vec3 position;
            if (!world.Position(node, position))
                return;
            const float x = float(world.KeyDown("D")) - float(world.KeyDown("A"));
            const float z = float(world.KeyDown("S")) - float(world.KeyDown("W"));
            const float length = std::sqrt(x * x + z * z);
            if (length == 0.0f)
                return;
            const float distance = static_cast<float>(speed * dt) / length;
            position.x += x * distance;
            position.z += z * distance;
            world.SetPosition(node, position);
        }

    private:
        phasma::World world;
        phasma::Node node;
        static constexpr float speed = 4.0f; // Local units per second; no collision response.
    };

} // namespace
PHASMA_NODE_SCRIPT(PlayerController)
