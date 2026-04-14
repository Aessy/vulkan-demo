#pragma once

#include <imgui.h>

#include <array>
#include <vector>
#include <string>

#include "Scene.h"
#include "Application.h"
#include "SolarSystem.h"

namespace gui
{

void createSolarSystemGui(SolarSystem& solar_system, Camera& cam);
void drawPlanetLabels(SolarSystem const& solar_system, Camera const& cam);
void createGui(RenderingState const& core, Application& application, SolarSystem* solar_system = nullptr);

}
