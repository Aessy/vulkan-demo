#include <algorithm>
#include <vector>
#include <numeric>
#include <math.h>
#include <iostream>
#include <map>

#include <glm/common.hpp>
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

            /*
            model.vertices.push_back(Vertex{.pos=top_left,.tex_coord=glm::vec2(0, 1)});
            model.vertices.push_back(Vertex{.pos=top_right,.tex_coord=glm::vec2(1, 1)});
            model.vertices.push_back(Vertex{.pos=bottom_left,.tex_coord=glm::vec2(0, 0)});
            model.vertices.push_back(Vertex{.pos=bottom_right,.tex_coord=glm::vec2(1, 0)});
            */

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

struct Comp
{
    bool operator()(glm::vec<3, float, glm::packed_highp> const& lhs, glm::vec<3, float, glm::packed_highp> const& rhs) const
    {
        return lhs.x < rhs.x ||
           lhs.x == rhs.x && (lhs.y < rhs.y || lhs.y == rhs.y && lhs.z < rhs.z);
    }
};

Model createModeFromHeightMap(std::string const& height_map_path, float size, float max_height)
{
    int width, height, channels {};
    auto pixels = stbi_load(height_map_path.c_str(), &width, &height, &channels, STBI_grey);

    if (width != height)
    {
        return {};
    }

    auto m = createFlatGround(width-1, size);

    size_t index = 0;

    for (auto& vertex : m.vertices)
    {
        int mapped_x = (vertex.pos.x+(size/2))/size*width;
        int mapped_y = (vertex.pos.z+(size/2))/size*height;

        size_t pos = (mapped_y*width) + mapped_x;
        unsigned char height = pixels[pos];

        vertex.pos.y = float(height)/256*max_height;
    }

    // TODO Calculate normal for each vertex.
    // Interpolate normals from all adjecent faces
    // Each vertex that is not an edge has 6 neighbour vertices.
    //
    // 1. In the loop over add each vertex to a map that is keyed on the vertex position. As value it contains a list of references
    //    to all vertices sharing the same position, and information about each triangle.
    //
    // 2. Loop over the map and calculate the normal for each point, the normal is shared between all neighbouring vertices.

    std::map<glm::vec3, std::vector<glm::vec3>, Comp> normal_map;
    for (auto index = m.indices.begin(); index != m.indices.end(); index += 3)
    {
        // Create the normal for the face index(0,1,2)
        auto v1 = m.vertices[*index].pos;
        auto v2 = m.vertices[*(index+1)].pos;
        auto v3 = m.vertices[*(index+2)].pos;

        // Face normal
        auto normal = glm::normalize(glm::cross(v2-v1, v3-v1));

        normal_map[v1].push_back(normal);
        normal_map[v2].push_back(normal);
        normal_map[v3].push_back(normal);
    }

    for (auto& vertex : m.vertices)
    {
        auto it = normal_map.find(vertex.pos);
        if (it != normal_map.end())
        {
            auto const& normals = it->second;
            auto normal = glm::normalize(std::accumulate(normals.begin(), normals.end(), glm::vec3(0,0,0)));
            vertex.normal = normal;
        }
    }

    stbi_image_free(pixels);

    std::cout << "Loaded height_map " << width << ":" << width << " " << channels << '\n';
    return m;

}
