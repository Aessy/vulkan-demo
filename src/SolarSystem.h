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
    glm::dvec3 sun_position_km{0.0};
    int  sun_scene_object_index{-1};
    double simulation_time_s{0.0};
    double time_scale{1'000'000.0};
};

SolarSystem createSolarSystem();
void updateSolarSystem(SolarSystem& ss, double delta_seconds);
Model createUVSphere(float radius, int stacks, int slices);
