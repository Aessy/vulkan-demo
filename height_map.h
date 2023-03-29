#pragma once

#include <string>

#include "Model.h"

Model createFlatGround(std::size_t size, float length);
void applyHeightMap(std::string const& height_map_path, Model& m);
