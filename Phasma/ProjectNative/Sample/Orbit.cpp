#include "ScriptModule.h"
#include <cmath>
namespace
{
    // Rebuilding this module changes the running game without rebuilding the engine.
    class Orbit
    {
    public:
        Orbit(const phasma::ScriptApi &api, phasma::Node node) : world(api), node(node)
        {
            if (!node)
                this->node = world.Find("ProjectNativeDemo");
            world.Log("Orbit instance created");
        }
        ~Orbit() { world.Log("Orbit instance destroyed"); }
        void Update(double dt)
        {
            if (!world.Valid(node))
                return;
            time += dt;
            world.SetPosition(node, {static_cast<float>(std::sin(time) * 2.0), 1.0f,
                                     static_cast<float>(std::cos(time) * 2.0)});
        }

    private:
        phasma::World world;
        phasma::Node node;
        double time = 0.0;
    };

} // namespace
PHASMA_NODE_SCRIPT(Orbit)
namespace
{
    const bool globalOrbit = phasma::RegisterScript<Orbit>("DemoOrbit", __FILE__, phasma::ScriptKind::Global);
}
