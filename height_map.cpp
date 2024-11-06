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
#include <vulkan/vulkan_core.h>

#include "Model.h"

struct BTN
{
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec3 normal;
};

BTN createBtn(glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3)
{
    glm::vec3 edge1 = p1 - p3;
    glm::vec3 edge2 = p2 - p3;

    glm::vec2 delta_uv1 = uv1 - uv3;
    glm::vec2 delta_uv2 = uv2 - uv3;

    float f = 1.0f / (delta_uv1.x * delta_uv2.y - delta_uv2.x * delta_uv2.y);

    glm::vec3 tangent;
    tangent.x = f * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
    tangent.y = f * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
    tangent.z = f * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);

    glm::vec3 bitangent;
    bitangent.x = f * (-delta_uv2.x * edge1.x + delta_uv1.x * edge2.x);
    bitangent.y = f * (-delta_uv2.x * edge1.y + delta_uv1.x * edge2.y);
    bitangent.z = f * (-delta_uv2.x * edge1.z + delta_uv1.x * edge2.z);

    glm::vec3 normal(0,1,0);

    return BTN{glm::normalize(tangent), glm::normalize(bitangent), glm::normalize(normal)};

}

Model createFlatGround(std::size_t size, float length, size_t texture_size)
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

            glm::vec3 top_left  (x_top_left, 0, y_top_left);                             // 2      3 
            glm::vec3 top_right (x_top_left+x_interval, 0, y_top_left);                  // 1
            glm::vec3 bottom_left (x_top_left, 0, y_top_left+y_interval);                //        4
            glm::vec3 bottom_right (x_top_left+x_interval, 0, y_top_left+y_interval);    // 0      5

            glm::vec2 norm_top_left((float)x/(x_size+1), 1.0-((float)y/(y_size+1)));
            glm::vec2 norm_top_right((float)(x+1)/(x_size+1), 1.0-((float)y/(y_size+1)));
            glm::vec2 norm_bottom_left((float)x/(x_size+1), 1.0-(float(y+1)/(y_size+1)));
            glm::vec2 norm_bottom_right((float)(x+1)/(x_size+1), 1.0-(float(y+1)/(y_size+1)));

            size_t x_texture = x % (texture_size+1);
            size_t y_texture = y % (texture_size+1);

            glm::vec2 tex_top_left((float)x_texture/(texture_size+1), 1.0-((float)y_texture/(texture_size+1)));
            glm::vec2 tex_top_right((float)(x_texture+1)/(texture_size+1), 1.0-((float)y_texture/(texture_size+1)));
            glm::vec2 tex_bottom_left((float)x_texture/(texture_size+1), 1.0-(float(y_texture+1)/(texture_size+1)));
            glm::vec2 tex_bottom_right((float)(x_texture+1)/(texture_size+1), 1.0-(float(y_texture+1)/(texture_size+1)));

            auto btn   = createBtn(top_right, top_left, bottom_right, norm_top_right, norm_top_left, norm_bottom_right);
            auto btn_2 = createBtn(bottom_left, bottom_right, top_left, norm_bottom_left, norm_bottom_right, norm_top_left);

            // TODO: Make BTN per triangle. I think we need two more vertices per quad

            glm::vec3 normal(0,1,0);
            model.vertices.push_back(Vertex{.pos=bottom_right,.tex_coord=tex_bottom_right, .normal=normal, .normal_coord=norm_bottom_right, .tangent=btn_2.tangent, .bitangent=btn_2.bitangent});
            model.vertices.push_back(Vertex{.pos=bottom_left,.tex_coord=tex_bottom_left, .normal=normal, .normal_coord=norm_bottom_left, .tangent=btn_2.tangent, .bitangent=btn_2.bitangent});
            model.vertices.push_back(Vertex{.pos=top_left,.tex_coord=tex_top_left, .normal=normal, .normal_coord=norm_top_left, .tangent=btn_2.tangent, .bitangent=btn_2.bitangent});

            model.vertices.push_back(Vertex{.pos=top_left,.tex_coord=tex_top_left, .normal=normal, .normal_coord=norm_top_left, .tangent=btn.tangent, .bitangent=btn.bitangent});
            model.vertices.push_back(Vertex{.pos=top_right,.tex_coord=tex_top_right, .normal=normal, .normal_coord=norm_top_right, .tangent=btn.tangent, .bitangent=btn.bitangent});
            model.vertices.push_back(Vertex{.pos=bottom_right,.tex_coord=tex_bottom_right, .normal=normal, .normal_coord=norm_bottom_right, .tangent=btn.tangent, .bitangent=btn.bitangent});


            model.indices.push_back(count+0);
            model.indices.push_back(count+1);
            model.indices.push_back(count+2);

            model.indices.push_back(count+3);
            model.indices.push_back(count+4);
            model.indices.push_back(count+5);

            count += 6;
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

Model createModelFromHeightMap(std::string const& height_map_path, float size, float max_height)
{
    int width, height, channels {};
    auto pixels = stbi_load(height_map_path.c_str(), &width, &height, &channels, STBI_grey);

    if (width != height)
    {
        return {};
    }

    auto m = createFlatGround(width-1, size, 8);

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

    for (int i = 0; i < m.vertices.size(); ++i)
    {
        auto &p1 = m.vertices[i];

        glm::vec3 init_vec;

        if (std::abs(p1.normal.x) < std::abs(p1.normal.y))
        {
            if (std::abs(p1.normal.x) < std::abs(p1.normal.z))
            {
                init_vec = glm::cross(p1.normal, glm::vec3(1,0,0));
            }
            else
            {
                init_vec = glm::cross(p1.normal, glm::vec3(0,0,1));
            }
        }
        else
        {
            if (std::abs(p1.normal.y) < std::abs(p1.normal.z))
            {
                init_vec = glm::cross(p1.normal, glm::vec3(0,1,0));
            }
            else
            {
                init_vec = glm::cross(p1.normal, glm::vec3(0,0,1));
            }
        }

        glm::vec3 tangent = glm::normalize(init_vec - glm::dot(p1.normal, init_vec) * p1.normal);
        glm::vec3 bitangent = glm::normalize(glm::cross(p1.normal, tangent));
        p1.tangent = tangent;
        p1.bitangent = bitangent;
    }

    stbi_image_free(pixels);

    std::cout << "Loaded height_map " << width << ":" << width << " " << channels << '\n';
    return m;

}
