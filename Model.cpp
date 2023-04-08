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
