#pragma once
#include "API/Vertex.h"

namespace pe::terrain
{
    // A low-poly prop mesh in template space: origin at the base (y = 0 sits on the terrain),
    // +Y up, roughly unit-sized footprint. Stamped into terrain tiles by the scatter system.
    struct ScatterTemplate
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        bool collide = true; // false = excluded from the terrain tile collider (e.g. grass)
    };

    // Builtin procedural templates: "tree", "rock", "grass". Returns an empty template (no
    // vertices) for unknown names.
    ScatterTemplate BuildScatterTemplate(const std::string &name);
} // namespace pe::terrain
