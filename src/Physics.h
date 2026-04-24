#pragma once

#include <glm/glm.hpp>
#include <span>

struct Attractor {
    glm::dvec3 pos_begin;
    glm::dvec3 pos_end;
    double      GM;
};

// Leapfrog KDK (Kick-Drift-Kick) symplectic integrator.
// Conserves a modified energy exactly, keeping orbits stable over arbitrary
// simulation lengths, provided dt is fixed.
// Each attractor supplies positions at the start and end of the step so that
// moving attractors are handled correctly.
// extra_accel is added to both kicks (e.g. constant thrust).
inline void leapfrogKDK(glm::dvec3& position, glm::dvec3& velocity,
                         std::span<Attractor const> attractors, double dt,
                         glm::dvec3 extra_accel = glm::dvec3(0.0))
{
    auto gravAccel = [](glm::dvec3 const& pos, glm::dvec3 const& attractor, double GM) {
        glm::dvec3 r  = pos - attractor;
        double     rm = glm::length(r);
        return -(GM / (rm * rm * rm)) * r;
    };

    glm::dvec3 acc0 = extra_accel;
    for (auto const& a : attractors)
        acc0 += gravAccel(position, a.pos_begin, a.GM);
    glm::dvec3 vel_half = velocity + acc0 * (dt * 0.5);
    position           += vel_half * dt;

    glm::dvec3 acc1 = extra_accel;
    for (auto const& a : attractors)
        acc1 += gravAccel(position, a.pos_end, a.GM);
    velocity = vel_half + acc1 * (dt * 0.5);
}
