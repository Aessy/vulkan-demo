#include "SolarSystem.h"

#include <glm/gtc/constants.hpp>
#include <numbers>
#include <ranges>

#include <spdlog/spdlog.h>

using glm::dvec3;

static constexpr double GM_SUN = 1.32712440018e11; // km^3 / s^2

// Leapfrog KDK (Kick-Drift-Kick) symplectic integrator.
// Conserves a modified energy exactly, keeping circular orbits stable
// over arbitrary simulation lengths.
static void leapfrogKDK(PlanetState& state, double dt)
{
    double r0      = glm::length(state.position_km);
    dvec3  acc     = -(GM_SUN / (r0 * r0 * r0)) * state.position_km;

    dvec3 vel_half    = state.velocity_km + acc * (dt * 0.5);   // half-kick
    state.position_km += vel_half * dt;                          // drift

    double r1      = glm::length(state.position_km);
    dvec3  acc_new = -(GM_SUN / (r1 * r1 * r1)) * state.position_km;
    state.velocity_km = vel_half + acc_new * (dt * 0.5);        // half-kick
}


SolarSystem createSolarSystem()
{
    SolarSystem ss;

    // Index 0 = Sun, indices 1-8 = Mercury through Neptune
    ss.defs = {
    // name       radius_km  mass_kg    sma_km      ecc  period_s     tilt     rot_s      atm    atm_color                    atm_scale  diff  norm  rough  metal  emis  albedo

    { "Sun",      696340.0,  1.989e30,  0.0,        0.0, 0.0,         0.0,     2.16e6,    false, {1.0f, 0.9f, 0.7f}, 0.00f, -1, -1, 1.0f, 0.0f, 1.0f, {1.0f, 0.92f, 0.35f}, {}, {} },

    { "Mercury",  2439.7,    3.301e23,  57.9e6,     0.0, 7.6e6,       0.034,   5.067e6,   false, {0.7f, 0.7f, 0.7f}, 0.00f, -1, -1, 0.8f, 0.0f, 0.0f, {0.55f, 0.52f, 0.50f},
      { 1.738e7, 2.393e6, 4.452e7 },
      { -5.62e1, 4.18e0, 3.52e1 } },

    { "Venus",    6051.8,    4.867e24, 108.2e6,    0.0, 19.41e6,     3.096,   2.1e7,     true,  {0.9f, 0.8f, 0.5f}, 0.05f, -1, -1, 0.7f, 0.0f, 0.0f, {0.92f, 0.84f, 0.55f},
      { -1.006e8, 6.056e6, 3.864e7 },
      { -1.24e1, 1.01e0, -3.27e1 } },

    { "Earth",    6371.0,    5.972e24, 149.6e6,    0.0, 31.56e6,     0.409,   86400.0,   true,  {0.4f, 0.6f, 1.0f}, 0.05f, -1, -1, 0.5f, 0.0f, 0.0f, {0.18f, 0.44f, 0.72f},
      { -2.645341e7, 2.784000e3, 1.439770e8 },
      { -2.981230e1, 1.140000e-4, -5.218140e0 } },

    { "Mars",     3389.5,    6.417e23, 227.9e6,    0.0, 59.36e6,     0.440,   88775.0,   true,  {0.8f, 0.4f, 0.2f}, 0.03f, -1, -1, 0.7f, 0.0f, 0.0f, {0.80f, 0.35f, 0.18f},
      { 1.946e8, -5.02e6, -3.978e7 },
      { 6.53e0, -6.45e-1, 2.57e1 } },

    { "Jupiter",  69911.0,   1.898e27, 778.5e6,    0.0, 374.0e6,     0.054,   35730.0,   true,  {0.8f, 0.7f, 0.6f}, 0.04f, -1, -1, 0.4f, 0.0f, 0.0f, {0.75f, 0.63f, 0.44f},
      { -5.210e8, 9.18e6, 5.984e8 },
      { -9.55e0, 2.24e-1, -6.98e0 } },

    { "Saturn",   58232.0,   5.683e26, 1432.0e6,   0.0, 929.3e6,     0.466,   38364.0,   true,  {0.9f, 0.85f, 0.7f}, 0.06f, -1, -1, 0.4f, 0.0f, 0.0f, {0.88f, 0.78f, 0.52f},
      { 1.352e9, -3.31e7, -4.964e8 },
      { 2.61e0, -2.55e-1, 9.18e0 } },

    { "Uranus",   25362.0,   8.681e25, 2867.0e6,   0.0, 2651.0e6,    1.706,   62064.0,   true,  {0.5f, 0.8f, 0.9f}, 0.05f, -1, -1, 0.3f, 0.0f, 0.0f, {0.52f, 0.82f, 0.84f},
      { 1.662e9, -1.85e7, 2.267e9 },
      { -5.29e0, 7.41e-2, 3.66e0 } },

    { "Neptune",  24622.0,   1.024e26, 4515.0e6,   0.0, 5200.0e6,    0.494,   57996.0,   true,  {0.3f, 0.5f, 0.9f}, 0.05f, -1, -1, 0.3f, 0.0f, 0.0f, {0.22f, 0.40f, 0.88f},
      { 2.845e9, -5.32e7, -9.553e8 },
      { 1.14e0, -1.26e-1, 3.01e0 } }
};

    ss.states.resize(ss.defs.size());
    ss.show_label.resize(ss.defs.size(), false);

    for (std::size_t i = 0; i < ss.defs.size(); ++i)
    {
        auto const& def    = ss.defs[i];
        auto&       state  = ss.states[i];
        state.mean_anomaly     = glm::radians(static_cast<double>(i) * 40.0);
        state.position_km      = def.init_position;
        state.prev_position_km = def.init_position;  // avoid lerp-from-origin on frame 0
        state.velocity_km      = def.init_velocity;
    }

    return ss;
}

void updateSolarSystem(SolarSystem& ss, double delta_seconds)
{
    ss.simulation_time_s += delta_seconds * ss.time_scale;

    constexpr double dt = 3600.0; // fixed physics step: 1 hour
    while (ss.simulation_time_s >= dt)
    {
        ss.simulation_time_s -= dt;

        for (auto&& [def, state] : std::views::zip(ss.defs, ss.states))
        {
            state.prev_position_km    = state.position_km;
            state.prev_rotation_angle = state.rotation_angle;

            if (def.semi_major_axis_km <= 0.0) continue; // Sun is stationary

            leapfrogKDK(state, dt);

            if (def.rotation_period_s > 0.0)
                state.rotation_angle += (2.0 * std::numbers::pi_v<double> / def.rotation_period_s) * dt;
        }
    }

    // Fraction of the current step elapsed — drives rendering interpolation.
    ss.render_alpha = ss.simulation_time_s / dt;
}

Model createUVSphere(float radius, int stacks, int slices)
{
    Model model;

    for (int i = 0; i <= stacks; ++i)
    {
        float theta    = (float)i / (float)stacks * std::numbers::pi_v<float>;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int j = 0; j <= slices; ++j)
        {
            float phi    = (float)j / (float)slices * 2.0f * std::numbers::pi_v<float>;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            Vertex v;
            v.pos    = glm::vec3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi) * radius;
            v.normal = glm::normalize(glm::vec3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi));

            v.tex_coord   = glm::vec2((float)j / (float)slices, (float)i / (float)stacks);
            v.normal_coord = v.tex_coord;

            // Tangent = d/dphi of normalized pos
            v.tangent   = glm::normalize(glm::vec3(-sinPhi, 0.0f, cosPhi));
            v.bitangent = glm::cross(v.normal, v.tangent);

            model.vertices.push_back(v);
        }
    }

    // Indices — two CCW triangles per quad
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            uint32_t a = (uint32_t)(i * (slices + 1) + j);
            uint32_t b = a + (uint32_t)(slices + 1);

            model.indices.push_back(a);
            model.indices.push_back(b);
            model.indices.push_back(a + 1);

            model.indices.push_back(b);
            model.indices.push_back(b + 1);
            model.indices.push_back(a + 1);
        }
    }

    return model;
}
