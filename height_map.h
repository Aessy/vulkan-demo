#pragma once

#include <string>

#include "Model.h"

Model createFlatGround(std::size_t size, float length, std::size_t texture_size);
Model createModelFromHeightMap(std::string const& height_map_path, float size, float max_height);
