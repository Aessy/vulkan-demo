#include <vector>
#include <math.h>
#include <iostream>

#include <glm/glm.hpp>
#include "glm/gtx/string_cast.hpp"

#include <stb/stb_image.h>

#include "Model.h"

Model createFlatGround(std::size_t size, float length)
{
    size_t x_size = size;
    size_t y_size = size;

    float x_interval = float(length)/x_size;
    float y_interval = float(length)/y_size;

    // x*y = 4
    // 4 boxes
    //
    

    float x_left = 0.0f - (length/2);
    float y_left = 0.0f - (length/2);

    Model model{};
    size_t count = 0;
    for (size_t y = 0; y < y_size; ++y)
    {
        float y_top_left = y_left  + y_interval*(y%y_size);
        for (size_t x = 0; x < x_size; ++x)
        {
            float x_top_left = x_left + x_interval*(x%x_size);

            glm::vec3 top_left  (x_top_left, 0, y_top_left);
            glm::vec3 top_right (x_top_left+x_interval, 0, y_top_left);
            glm::vec3 bottom_left (x_top_left, 0, y_top_left+y_interval);
            glm::vec3 bottom_right (x_top_left+x_interval, 0, y_top_left+y_interval);

            model.vertices.push_back(Vertex{.pos=top_left,.tex_coord=glm::vec2(top_left.x, top_left.y)});
            model.vertices.push_back(Vertex{.pos=top_right,.tex_coord=glm::vec2(top_right.x, top_right.y)});
            model.vertices.push_back(Vertex{.pos=bottom_left,.tex_coord=glm::vec2(bottom_left.x, bottom_left.y)});
            model.vertices.push_back(Vertex{.pos=bottom_right,.tex_coord=glm::vec2(bottom_left.x, bottom_right.y)});

            model.indices.push_back(count+3);
            model.indices.push_back(count+1);
            model.indices.push_back(count+0);

            model.indices.push_back(count+0);
            model.indices.push_back(count+2);
            model.indices.push_back(count+3);

            count += 4;
        }
    }
    return model;
}

void applyHeightMap(std::string const& height_map_path, Model& m)
{
    int width, height, channels {};

    auto pixels = stbi_load(height_map_path.c_str(), &width, &height, &channels, STBI_grey);

    size_t index = 0;

    for (auto& vertex : m.vertices)
    {
        int mapped_x = (vertex.pos.x+(200.0f/2))/200*width;
        int mapped_y = (vertex.pos.z+(200.0f/2))/200*height;

        size_t pos = (mapped_y*width) + mapped_x;
        unsigned char height = pixels[pos];

        vertex.pos.y = float(height)/256*50;
    }

    std::cout << "Loaded height_map " << width << ":" << width << " " << channels << '\n';
}
