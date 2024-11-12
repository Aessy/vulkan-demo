#include "Model.h"
#include <stdexcept>

#include <assimp/Importer.hpp>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

#include <spdlog/spdlog.h>

#include <vector>
#include <string>
#include <iostream>

static_assert(sizeof(int) == 4, "");
static_assert(sizeof(float) == 4, "");
static_assert(sizeof(glm::vec4) == 16, "");


int Models::loadModelAssimp(std::string const& path)
{
    Assimp::Importer importer;
    auto *scene = importer.ReadFile(path,  aiProcess_Triangulate
                                                                 | aiProcess_CalcTangentSpace
                                                                 | aiProcess_FlipWindingOrder);

    if (!scene)
    {
        return -1;
    }

    Model model;
    model.path = path;
    if (scene->mNumMeshes > 0)
    {
        auto* mesh = scene->mMeshes[0];

        for (int i = 0; i < mesh->mNumVertices; ++i)
        {
            auto const vertex = mesh->mVertices[i];

            Vertex vert;
            vert.pos = glm::vec3(vertex.x, vertex.y, vertex.z);

            if (mesh->HasNormals())
            {
                auto const normal = mesh->mNormals[i];
                vert.normal = glm::vec3(normal.x, normal.y, normal.z);
            }

            if (mesh->HasTextureCoords(0))
            {
                auto const tex_coord = mesh->mTextureCoords[0][i];
                vert.tex_coord = glm::vec2(tex_coord.x, tex_coord.y);
            }

            if (mesh->HasTangentsAndBitangents())
            {
                auto const tangent = mesh->mTangents[i];
                auto const bitangent = mesh->mBitangents[i];

                vert.tangent = glm::vec3(tangent.x, tangent.y, tangent.z);
                vert.bitangent = glm::vec3(bitangent.x, bitangent.y, bitangent.z);

                if (vert.tangent.length() == 0 ||
                    vert.bitangent.length() == 0)
                {
                    throw std::runtime_error("Invalid tangent/bitangent");
                }
            }
            else
            {
                throw std::runtime_error("No tangent or bitangent");
            }

            model.vertices.push_back(vert);
            //model.indices.push_back(model.indices.size());
        }

        for (int i = 0; i < mesh->mNumFaces; ++i)
        {
            auto face = mesh->mFaces[i];
            for (int index = 0; index < face.mNumIndices; ++index)
            {
                model.indices.push_back(face.mIndices[index]);
            }
        }
    }
    else
    {
        return -1;
    }

    models.insert({model.id, model});
    return model.id;
}

int Models::loadModel(std::string const& model_path)
{
    /*
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path.c_str()))
    {
        spdlog::warn("Failed loading model");
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

            model.vertices.push_back(vertex);
            model.indices.push_back(model.indices.size());
        }
    }

    models.insert({model.id, model});

    return model.id;
    */
    return -1;
}

Model createBox()
{
        // Demo box vertices
    std::vector<Vertex> vertices {
        //Position           // Color            // UV   // Normal
        // Front
        {{-2.5, -2.5, 2.5},  {0, 0}, {0,0,1}}, //0  0
        {{ 2.5, -2.5, 2.5},  {1, 0}, {0,0,1}}, //1  1
        {{-2.5,  2.5, 2.5},  {0, 1}, {0,0,1}}, //2  2
        {{ 2.5,  2.5, 2.5},  {1, 1}, {0,0,1}}, //3  3
                                                          //
        // Up
        {{-2.5,  2.5, 2.5},  {0, 0}, {0,1,0}}, //2  4
        {{ 2.5,  2.5, 2.5},  {1, 0}, {0,1,0}}, //3  5
        {{-2.5,  2.5, -2.5}, {0, 1}, {0,1,0}}, //6  6
        {{ 2.5,  2.5, -2.5}, {1, 1}, {0,1,0}}, //7  7

        // Down
        {{-2.5, -2.5, 2.5},  {0, 0}, {0,-1,0}}, //0  8
        {{ 2.5, -2.5, 2.5},  {1, 0}, {0,-1,0}}, //1  9
        {{-2.5, -2.5, -2.5}, {0, 1}, {0,-1,0}}, //4 10
        {{ 2.5, -2.5, -2.5}, {1, 1}, {0,-1,0}}, //5 11
                                                          //
        // Left
        {{-2.5, -2.5, 2.5},  {0, 0}, {-1,0,0}}, //0 12
        {{-2.5, -2.5, -2.5}, {1, 0}, {-1,0,0}}, //4 13
        {{-2.5,  2.5, 2.5},  {0, 1}, {-1,0,0}}, //2 14
        {{-2.5,  2.5, -2.5}, {1, 1}, {-1,0,0}}, //6 15
                                                          //
        // Right
        {{ 2.5, -2.5, 2.5},  {1, 0}, {1,0,0}}, //1 16
        {{ 2.5, -2.5, -2.5}, {0, 0}, {1,0,0}}, //5 17
        {{ 2.5,  2.5, 2.5},  {1, 1}, {1,0,0}}, //3 18
        {{ 2.5,  2.5, -2.5}, {0, 1}, {1,0,0}}, //7 19

        // Back
        {{-2.5, -2.5, -2.5}, {1, 0}, {0,0,-1}}, //4 20
        {{ 2.5, -2.5, -2.5}, {0, 0}, {0,0,-1}}, //5 21
        {{-2.5,  2.5, -2.5}, {1, 1}, {0,0,-1}}, //6 22
        {{ 2.5,  2.5, -2.5}, {0, 1}, {0,0,-1}}  //7 23
    };
    const std::vector<uint32_t> indices = {
        // Front
        0, 2, 3,
        3, 1, 0,

        // Top
        7, 5, 4,
        4, 6, 7,

        // Bottom
        11, 10, 8,
        8, 9, 11,

        // Left
        12, 13, 15,
        15, 14, 12,

        // Right
        16, 18, 19,
        19, 17, 16,

        // Back
        20, 21, 23,
        23, 22, 20
    };

    Model model;

    model.indices = indices;
    model.vertices = vertices;
    model.path = "Box";

    return model;
}