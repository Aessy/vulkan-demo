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

/*
const char* const items[]{"Uniform", "Storage", "Textures"};
const char* const buffer_type[]{"Model", "World"};


void createBinding(RenderingSystem& system)
{
    if (ImGui::BeginPopup("create_binding_popup"))
    {
        static Binding new_binding = {};
        ImGui::SeparatorText("Create Binding");
        ImGui::InputText("Name", new_binding.name.data(), new_binding.name.size());
        ImGui::Combo("Binding Type", &new_binding.type, items, 3);
        ImGui::InputInt("Size", &new_binding.size, 1, 1);
        ImGui::Checkbox("Vertex shader", &new_binding.vertex);
        ImGui::Checkbox("Fragment shader", &new_binding.fragment);
        if (ImGui::Button("Create"))
        {
            system.bindings.push_back(new_binding);
            new_binding = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void createProgram(RenderingSystem& system)
{
    if (ImGui::BeginPopup("create_program"))
    {
        static Program new_program = {};
        ImGui::SeparatorText("Create Program");
        ImGui::InputText("Name", new_program.name.data(), new_program.name.size());
        ImGui::InputText("Vertex shader", new_progracreate_buffer_popup

}


void createBuffer(RenderingSystem& system)
{
    if (ImGui::BeginPopup("create_buffer_popup"))
    {
        static Buffer new_buffer = {};
        ImGui::SeparatorText("Create buffer");
        ImGui::InputText("Name", new_buffer.name.data(), new_buffer.name.size());
        ImGui::Combo("Buffer Type", &new_buffer.type, buffer_type, 2);
        ImGui::InputInt("Size", &new_buffer.size, 1, 1);

        static int current_binding = -1;

        if (system.bindings.size() > 0)
        {
            if (current_binding == -1)
            {
                current_binding = 0;
                new_buffer.binding = system.bindings[0];
            }

            char const* current_item = system.bindings[current_binding].name.data();
            if (ImGui::BeginCombo("Binding", current_item))
            {
                for (size_t i = 0; i < system.bindings.size(); ++i)
                {
                    bool const is_selected = (current_binding == i);
                    if (ImGui::Selectable(system.bindings[i].name.data(), is_selected))
                    {
                        current_binding = i;
                        new_buffer.binding = system.bindings[i];
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                 }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("Create"))
        {
            system.buffers.push_back(new_buffer);
            new_buffer = {};
            current_binding = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
*/

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

void showLight(LightBufferObject const& light)
{
    if (ImGui::BeginPopup("light"))
    {
        ImGui::Text("Pos: x:%f, y:%f, z:%f",light.position.x, light.position.y, light.position.z);
        ImGui::EndPopup();
    }
}
void showObject(Object& obj)
{
    if (ImGui::BeginPopup("object"))
    {
        std::cout << "object\n";
        ImGui::Text("Position");
        ImGui::InputFloat("Pos x", &obj.position.x, 1.0f, 10.0f);
        ImGui::InputFloat("Pos y", &obj.position.y, 1.0f, 10.0f);
        ImGui::InputFloat("Pos z", &obj.position.z, 1.0f, 10.0f);
        ImGui::Text("Rotation");
        ImGui::InputFloat("X", &obj.rotation.x, 1.0f, 10.0f);
        ImGui::InputFloat("Y", &obj.rotation.y, 1.0f, 10.0f);
        ImGui::InputFloat("Z", &obj.rotation.z, 1.0f, 10.0f);
        ImGui::Text("Angle");
        ImGui::DragFloat("Angle", &obj.angel, 0.01, 0, 360);
        ImGui::EndPopup();
    }
}
void showScene(Scene& scene)
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
        ImGui::TreePop();
    }

    for (auto& prog: scene.objects)
    {
        for (auto& obj : prog.second)
        {
            if (ImGui::TreeNode("Object"))
            {
                showObject(obj);
                ImGui::Text("Pos: x:%f, y:%f, z:%f",obj.position.x, obj.position.y, obj.position.z);
                ImGui::Text("Angle: %f", obj.angel);
                ImGui::Text("Scale: %f", obj.scale);
                ImGui::Text("Indices: %d", obj.mesh.indices_size);
                ImGui::Text("Material: %d", prog.first);
                ImGui::Text("Texture: %d", obj.texture_index);
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

void showMeshes(std::map<std::string, DrawableMesh>& meshes)
{
    ImGui::BeginChild("Meshes", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto const& mesh : meshes)
    {
        ImGui::Text("%s", mesh.first.c_str());
    }

    ImGui::EndChild();

}

void createGui(Application& application)
{
    ImGui::Begin("Vulkan rendering engine", nullptr, ImGuiWindowFlags_MenuBar);
    if (ImGui::CollapsingHeader("Scene"))
    {
        showScene(application.scene);
    }
    if (ImGui::CollapsingHeader("Meshes"))
    {
        showMeshes(application.meshes);
    }

    ImGui::End();

}
/*
void createGui(RenderingSystem& system)
{
    ImGui::Begin("RenderingSystem", nullptr, ImGuiWindowFlags_MenuBar);

    if (ImGui::CollapsingHeader("Bindings"))
    {
        if (ImGui::Button("Create Binding"))
        {
            ImGui::OpenPopup("create_binding_popup");
        }

        ImGui::BeginChild("Bindings", ImVec2(-1, 100), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < system.bindings.size(); ++i)
        {
            ImGui::Text("%u: %s", i, system.bindings[i].name.data());
        }
        ImGui::EndChild();

    }
    if (ImGui::CollapsingHeader("Buffers"))
    {
        if (ImGui::Button("Create buffer"))
        {
            ImGui::OpenPopup("create_buffer_popup");
        }

        ImGui::BeginChild("Buffers", ImVec2(-1, 100), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < system.buffers.size(); ++i)
        {
            ImGui::Text("%u: %s", i, system.buffers[i].name.data());
        }
        ImGui::EndChild();
    }
    if (ImGui::CollapsingHeader("Textures"))
    {
        if (ImGui::Button("Add"))
        {
        }
    }

    createBuffer(system);
    createBinding(system);

    ImGui::End();
}
*/

}
