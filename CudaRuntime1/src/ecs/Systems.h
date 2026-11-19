#pragma once

#include "ecs/ECS.h"

namespace vt::systems {

// Advances every entity that has both a Transform and a Rotator.
void applyRotation(Registry &registry, float deltaSeconds);

} // namespace vt::systems
