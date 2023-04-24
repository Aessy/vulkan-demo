#include "Model.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <vector>
#include <string>
#include <iostream>

int Models::loadModel(std::string const& model_path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path.c_str()))
    {
        std::cout << "Failed loading model\n";
        return -1;
    }

    Model model{};
    model.path = model_path;

    for (auto const& shape : shapes)
    {
        for (auto const& index : shape.mesh.indices)
        {
            Vertex vertex;

            vertex.pos = {
                  attrib.vertices[3 * index.vertex_index + 0]
                , attrib.vertices[3 * index.vertex_index + 1]
                , attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.tex_coord = {
                  attrib.texcoords[2 * index.texcoord_index + 0]
                , 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            vertex.normal = {
                  attrib.normals[3 * index.normal_index + 0]
                , attrib.normals[3 * index.normal_index + 1]
                , attrib.normals[3 * index.normal_index + 2]
            };

            vertex.color = {0.5f, 0.1f, 0.3f};

            model.vertices.push_back(vertex);
            model.indices.push_back(model.indices.size());
        }
    }

    models.insert({model.id, model});

    return model.id;
}

Model createBox()
{
        // Demo box vertices
    std::vector<Vertex> vertices {
        //Position           // Color            // UV   // Normal
        // Front
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,0,1}}, //0  0
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,0,1}}, //1  1
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 1}, {0,0,1}}, //2  2
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 1}, {0,0,1}}, //3  3
                                                          //
        // Up
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,1,0}}, //2  4
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,1,0}}, //3  5
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,1,0}}, //6  6
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,1,0}}, //7  7

        // Down
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {0,-1,0}}, //0  8
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {0,-1,0}}, //1  9
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,-1,0}}, //4 10
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,-1,0}}, //5 11
                                                          //
        // Left
        {{-0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 0}, {-1,0,0}}, //0 12
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 0}, {-1,0,0}}, //4 13
        {{-0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {0, 1}, {-1,0,0}}, //2 14
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {-1,0,0}}, //6 15
                                                          //
        // Right
        {{ 0.5, -0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 0}, {1,0,0}}, //1 16
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 0}, {1,0,0}}, //5 17
        {{ 0.5,  0.5, 0.5},  {1.0f, 0.0f, 0.0f}, {1, 1}, {1,0,0}}, //3 18
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {1,0,0}}, //7 19

        // Back
        {{-0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 0}, {0,0,-1}}, //4 20
        {{ 0.5, -0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 0}, {0,0,-1}}, //5 21
        {{-0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {1, 1}, {0,0,-1}}, //6 22
        {{ 0.5,  0.5, -0.5}, {1.0f, 0.0f, 0.0f}, {0, 1}, {0,0,-1}}  //7 23
    };
    const std::vector<uint32_t> indices = {
        // Front
        0, 1, 3,
        3, 2, 0,

        // Top
        7, 6, 4,
        4, 5, 7,

        // Bottom
        11, 9, 8,
        8, 10, 11,

        // Left
        12, 14, 15,
        15, 13, 12,

        // Right
        16, 17, 19,
        19, 18, 16,

        // Back
        20, 22, 23,
        23, 21, 20
    };

    Model model;

    model.indices = indices;
    model.vertices = vertices;
    model.path = "Box";

    return model;
}