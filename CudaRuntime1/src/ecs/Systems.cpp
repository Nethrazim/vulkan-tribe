#include "ecs/Systems.h"

#include "ecs/Components.h"

namespace vt::systems {

void applyRotation(Registry &registry, float deltaSeconds) {
    registry.each<Rotator, Transform>([&](Entity entity) {
        const Rotator &spin = registry.get<Rotator>(entity);
        Transform &transform = registry.get<Transform>(entity);

        const float angle = spin.radiansPerSecond * deltaSeconds;
        const glm::quat step = glm::angleAxis(angle, glm::normalize(spin.axis));
        transform.rotation = glm::normalize(step * transform.rotation);
    });
}

} // namespace vt::systems
