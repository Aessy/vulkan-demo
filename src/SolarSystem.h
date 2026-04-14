#pragma once

#include "Model.h"

#include <glm/glm.hpp>
#include <vector>
#include <cmath>

struct PlanetDef {
    const char* name;
    double radius_km;
    double mass_kg;
    double semi_major_axis_km;
    double eccentricity;
    double orbital_period_s;
    double axial_tilt_rad;
    double rotation_period_s;
    bool   has_atmosphere;
    glm::vec3 atmosphere_color;
    float atmosphere_scale;
    int    diffuse_texture_index;
    int    normal_texture_index;
    float  roughness;
    float  metallic;
    float  emissive;
    glm::vec3 albedo_color;
};

struct PlanetState {
    double mean_anomaly{0.0};
    glm::dvec3 position_km{0.0};
    double rotation_angle{0.0};
    int    scene_object_index{-1};
};

struct SolarSystem {
    std::vector<PlanetDef>   defs;
    std::vector<PlanetState> states;
    std::vector<bool>        show_label;
    glm::dvec3 sun_position_km{0.0};
    int  sun_scene_object_index{-1};
    double simulation_time_s{0.0};
    double time_scale{1'000'000.0};
    bool paused{false};

    // Orbit ring rendering
    bool  show_orbits{true};
    float orbit_line_width{2.0f};
    float orbit_opacity{0.6f};
    bool  orbit_stippled{false};

    // Ecliptic grid
    bool  show_grid{true};
    float grid_line_width{1.0f};
    float grid_opacity{0.35f};
    int   grid_line_count{20};      // lines per side (total lines = 2*grid_line_count+1)
    float grid_spacing_km{5e8f};    // ~3.3 AU

    // Object selection (-1 = Sun/origin)
    int selected_body{-1};
};

SolarSystem createSolarSystem();
void updateSolarSystem(SolarSystem& ss, double delta_seconds);
Model createUVSphere(float radius, int stacks, int slices);
