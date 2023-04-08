#include "Gui.h"

#include <imgui.h>

#include <array>
#include <vector>
#include <string>
#include <iostream>

#include "Scene.h"

#include "Application.h"

namespace gui
{

void showModelTree(Model& model)
{
    if (ImGui::TreeNode((model.path + std::to_string(model.id)).c_str()))
    {
        ImGui::Text("Path: %s", model.path.c_str());
        ImGui::Text("Vertices: %d", model.vertices.size());
        ImGui::Text("Indices: %d", model.indices.size());

        ImGui::TreePop();
    }

}

void showModels(Models& models)
{
    ImGui::BeginChild("Models", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& model: models.models)
    {
        showModelTree(model.second);
    }

    ImGui::EndChild();

}

void showMeshTree(DrawableMesh& mesh, Models& models, std::string const& name)
{
    if (ImGui::TreeNode(name.c_str()))
    {
        ImGui::Text("Indices: %d", mesh.indices_size);
        if (models.models.contains(mesh.model_id))
        {
            showModelTree(models.models.at(mesh.model_id));
        }

        ImGui::TreePop();
    }
}

void showMeshes(Meshes& meshes, Models& models)
{
    ImGui::BeginChild("Meshes", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& mesh : meshes.meshes)
    {
        showMeshTree(mesh.second, models, (mesh.second.name + ": " + std::to_string(mesh.second.id).c_str()));
    }

    ImGui::EndChild();
}
void showCamera(Camera const& camera)
{
    if (ImGui::BeginPopup("camera"))
    {

        ImGui::Text("Pos: x:%f, y:%f, z:%f",camera.pos.x,
                                            camera.pos.y,
                                            camera.pos.z);
        ImGui::EndPopup();
    }
}

void showLight(LightBufferObject& light)
{
    if (ImGui::BeginPopup("light"))
    {
        ImGui::Text("Position");
        ImGui::DragFloat("Pos x", &light.position.x, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos y", &light.position.y, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos z", &light.position.z, 0.1f, -100.0f, 100);
        ImGui::Text("Strength");
        ImGui::DragFloat("Strength", &light.strength, 1.0f, 0.0f, 1000.0f);

        static float colors[3] {light.light_color.r, light.light_color.g, light.light_color.b};

        if (ImGui::ColorEdit3("Color", colors))
        {
            light.light_color.r = colors[0];
            light.light_color.g = colors[1];
            light.light_color.b = colors[2];
        }
        ImGui::EndPopup();
    }
}
void showObject(Object& obj)
{
    if (ImGui::BeginPopup("object"))
    {
        std::cout << "object\n";
        ImGui::Text("Position");
        ImGui::DragFloat("Pos x", &obj.position.x, 0.1f, -100.0f, 100.0f);
        ImGui::DragFloat("Pos y", &obj.position.y, 0.1f, -100.0f, 100.0f);
        ImGui::DragFloat("Pos z", &obj.position.z, 0.1f, -100.0f, 100.0f);
        ImGui::Text("Rotation");
        ImGui::InputFloat("X", &obj.rotation.x, 1.0f, 10.0f);
        ImGui::InputFloat("Y", &obj.rotation.y, 1.0f, 10.0f);
        ImGui::InputFloat("Z", &obj.rotation.z, 1.0f, 10.0f);
        ImGui::Text("Angle");
        ImGui::DragFloat("Angle", &obj.angel, 0.01, 0, 360);
        ImGui::EndPopup();
    }
}
void showScene(Scene& scene, Models& models)
{
    ImGui::BeginChild("Scene", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::TreeNode("Camera"))
    {
        Camera& camera = scene.camera;
        ImGui::Text("Pos: x:%f, y:%f, z:%f",camera.pos.x,
                                            camera.pos.y,
                                            camera.pos.z);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Light"))
    {
        auto& light = scene.light;
        ImGui::Text("Pos: x:%f, y:%f, z:%f",light.position.x, light.position.y, light.position.z);
        if (ImGui::Button("Edit"))
        {
            ImGui::OpenPopup("light");
        }

        showLight(light);
        ImGui::TreePop();
    }

    for (auto& prog: scene.objects)
    {
        for (auto& obj : prog.second)
        {
            if (ImGui::TreeNode((std::string(obj.mesh.name) + std::to_string(obj.id)).c_str()))
            {
                showObject(obj);
                ImGui::Text("Pos: x:%f, y:%f, z:%f",obj.position.x, obj.position.y, obj.position.z);
                ImGui::Text("Angle: %f", obj.angel);
                ImGui::Text("Scale: %f", obj.scale);
                ImGui::Text("Indices: %d", obj.mesh.indices_size);
                ImGui::Text("Material: %d", prog.first);
                ImGui::Text("Texture: %d", obj.texture_index);
                showMeshTree(obj.mesh, models, std::string("Mesh: ") + obj.mesh.name + " " + std::to_string(obj.mesh.id));
                if (ImGui::Button("Edit"))
                {
                    std::cout << "Open\n";
                    ImGui::OpenPopup("object");
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::EndChild();
}

void createGui(Application& application)
{
    ImGui::Begin("Vulkan rendering engine", nullptr, ImGuiWindowFlags_MenuBar);
    if (ImGui::CollapsingHeader("Scene"))
    {
        showScene(application.scene, application.models);
    }
    if (ImGui::CollapsingHeader("Meshes"))
    {
        showMeshes(application.meshes, application.models);
    }
    if (ImGui::CollapsingHeader("Models"))
    {
        showModels(application.models);
    }

    ImGui::End();

}

}
