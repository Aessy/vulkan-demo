#include "SolarSystem.h"

#include <glm/gtc/constants.hpp>
#include <cmath>

static constexpr double PI = 3.14159265358979323846;

SolarSystem createSolarSystem()
{
    SolarSystem ss;

    // Index 0 = Sun, indices 1-8 = Mercury through Neptune
    ss.defs = {
        // name       radius_km  mass_kg    sma_km      ecc  period_s     tilt     rot_s      atm    atm_color                    atm_scale  diff  norm  rough  metal  emis  albedo
        { "Sun",      696340.0,  1.989e30,  0.0,        0.0, 0.0,         0.0,     2.16e6,    false, {1.0f, 0.9f, 0.7f},          0.00f,     -1,   -1,   1.0f,  0.0f,  1.0f, {1.0f, 0.92f, 0.35f} },
        { "Mercury",  2439.7,    3.301e23,  57.9e6,     0.0, 7.6e6,       0.034,   5.067e6,   false, {0.7f, 0.7f, 0.7f},          0.00f,     -1,   -1,   0.8f,  0.0f,  0.0f, {0.55f, 0.52f, 0.50f} },
        { "Venus",    6051.8,    4.867e24,  108.2e6,    0.0, 19.41e6,     3.096,   2.1e7,     true,  {0.9f, 0.8f, 0.5f},          0.05f,     -1,   -1,   0.7f,  0.0f,  0.0f, {0.92f, 0.84f, 0.55f} },
        { "Earth",    6371.0,    5.972e24,  149.6e6,    0.0, 31.56e6,     0.409,   86400.0,   true,  {0.4f, 0.6f, 1.0f},          0.05f,     -1,   -1,   0.5f,  0.0f,  0.0f, {0.18f, 0.44f, 0.72f} },
        { "Mars",     3389.5,    6.417e23,  227.9e6,    0.0, 59.36e6,     0.440,   88775.0,   true,  {0.8f, 0.4f, 0.2f},          0.03f,     -1,   -1,   0.7f,  0.0f,  0.0f, {0.80f, 0.35f, 0.18f} },
        { "Jupiter",  69911.0,   1.898e27,  778.5e6,    0.0, 374.0e6,     0.054,   35730.0,   true,  {0.8f, 0.7f, 0.6f},          0.04f,     -1,   -1,   0.4f,  0.0f,  0.0f, {0.75f, 0.63f, 0.44f} },
        { "Saturn",   58232.0,   5.683e26,  1432.0e6,   0.0, 929.3e6,     0.466,   38364.0,   true,  {0.9f, 0.85f, 0.7f},         0.06f,     -1,   -1,   0.4f,  0.0f,  0.0f, {0.88f, 0.78f, 0.52f} },
        { "Uranus",   25362.0,   8.681e25,  2867.0e6,   0.0, 2651.0e6,    1.706,   62064.0,   true,  {0.5f, 0.8f, 0.9f},          0.05f,     -1,   -1,   0.3f,  0.0f,  0.0f, {0.52f, 0.82f, 0.84f} },
        { "Neptune",  24622.0,   1.024e26,  4515.0e6,   0.0, 5200.0e6,    0.494,   57996.0,   true,  {0.3f, 0.5f, 0.9f},          0.05f,     -1,   -1,   0.3f,  0.0f,  0.0f, {0.22f, 0.40f, 0.88f} },
    };

    ss.states.resize(ss.defs.size());
    ss.show_label.resize(ss.defs.size(), false);

    // Spread initial mean anomalies 40° apart
    for (int i = 0; i < (int)ss.defs.size(); ++i)
    {
        ss.states[i].mean_anomaly = glm::radians(i * 40.0);
        double M = ss.states[i].mean_anomaly;
        double a = ss.defs[i].semi_major_axis_km;
        ss.states[i].position_km = glm::dvec3(std::cos(M) * a, 0.0, std::sin(M) * a);
    }

    return ss;
}

void updateSolarSystem(SolarSystem& ss, double delta_seconds)
{
    ss.simulation_time_s += delta_seconds * ss.time_scale;

    for (int i = 0; i < (int)ss.defs.size(); ++i)
    {
        auto& def   = ss.defs[i];
        auto& state = ss.states[i];

        if (def.orbital_period_s > 0.0)
        {
            state.mean_anomaly += (2.0 * PI / def.orbital_period_s) * delta_seconds * ss.time_scale;
            double M = state.mean_anomaly;
            double a = def.semi_major_axis_km;
            state.position_km = glm::dvec3(std::cos(M) * a, 0.0, std::sin(M) * a);
        }

        if (def.rotation_period_s > 0.0)
        {
            state.rotation_angle += (2.0 * PI / def.rotation_period_s) * delta_seconds * ss.time_scale;
        }
    }
}

Model createUVSphere(float radius, int stacks, int slices)
{
    Model model;

    for (int i = 0; i <= stacks; ++i)
    {
        float theta    = (float)i / (float)stacks * glm::pi<float>(); // 0 .. PI
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int j = 0; j <= slices; ++j)
        {
            float phi    = (float)j / (float)slices * 2.0f * glm::pi<float>(); // 0 .. 2PI
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
