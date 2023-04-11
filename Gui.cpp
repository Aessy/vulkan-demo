#include "Gui.h"

#include <imgui.h>

#include <array>
#include <vector>
#include <string>
#include <iostream>

#include "Scene.h"
#include "height_map.h"

#include "Application.h"

namespace gui
{

void createModel(RenderingState const& state, Models& models)
{
    if (ImGui::BeginPopup("create_model"))
    {
        ImGui::Text("Create model");

        static const char* items[] = {"Box", "Grid"};
        static int item_current = 0;
        static float size = 1;
        static int boxes_per_row = 1;
        static std::array<char, 20> name {{}};
        ImGui::Combo("Mesh type", &item_current, items, 2);

        if (item_current == 0)
        {
            // TODO: Make box
            ImGui::Text("TODO");
        }
        else if (item_current == 1)
        {
            ImGui::InputText("Name", name.data(), name.size()-1);
            ImGui::InputFloat("Grid size", &size, 0.1f, 1.0f);
            ImGui::DragInt("Boxes per row", &boxes_per_row, 1, 1, 5000);
            ImGui::Text("Vertices: %d", boxes_per_row*boxes_per_row*4);
        }

        if (ImGui::Button("Create"))
        {
            if (item_current == 1)
            {
                Model m = createFlatGround(boxes_per_row, size);
                m.path = name.data();
                models.models.insert({m.id, m});
            }

            item_current = 0;
            size = 1;
            boxes_per_row = 1;
            name = {{}};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void createMesh(RenderingState const& core, Application& app)
{
    if (ImGui::BeginPopup("create_mesh"))
    {
        ImGui::Text("Create mesh");

        static auto current_item = app.models.models.begin();
        static int current_id = current_item->first;
        static std::array<char, 30> name{{}};
        if (ImGui::BeginCombo("Combo", app.models.models[current_id].path.c_str()))
        {
            for (auto const& item : app.models.models)
            {
                bool selected = current_id == item.first;
                if (ImGui::Selectable(item.second.path.c_str(), &selected))
                {
                    current_id = item.first;
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::InputText("Name", name.data(), name.size()-1);

        if (ImGui::Button("Create mesh"))
        {
            app.meshes.loadMesh(core,app.models.models[current_id], name.data());
            name = {{}};
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void createObject(Application& app)
{
    if (ImGui::BeginPopup("create_object"))
    {
        ImGui::Text("Create Object");

        static auto current_item = app.meshes.meshes.begin();
        static int current_id = current_item->first;
        static std::array<char, 30> name{{}};
        static float pos[3]{};
        static int texture_id{};
        static int material{};
        if (ImGui::BeginCombo("Combo", app.meshes.meshes[current_id].name.c_str()))
        {
            for (auto const& item : app.meshes.meshes)
            {
                bool selected = current_id == item.first;
                if (ImGui::Selectable(item.second.name.c_str(), &selected))
                {
                    current_id = item.first;
                }

                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        ImGui::InputText("Name", name.data(), name.size()-1);
        ImGui::DragFloat3("Position", pos, 0.1, -100, 100);
        ImGui::InputInt("Texture: ", &texture_id,1,32);
        ImGui::DragInt("Material: ", &material, 1, 0, app.programs.size()-1);
        // TODO more info like material, texture, etc

        if (ImGui::Button("Create object"))
        {
            auto obj = createObject(app.meshes.meshes.at(current_id), glm::vec3(pos[0], pos[1], pos[2]));
            obj.texture_index = texture_id;

            app.scene.objects[material].push_back(std::move(obj));
            name = {{}};
            pos[0] = 0;
            pos[1] = 0;
            pos[2] = 0;
            texture_id = 0;
            material = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

}

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

void showModels(RenderingState const& core, Models& models)
{
    ImGui::BeginChild("Models", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& model: models.models)
    {
        showModelTree(model.second);
    }

    ImGui::EndChild();

    if (ImGui::Button("Create model"))
    {
        ImGui::OpenPopup("create_model");
    }
    createModel(core, models);
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

void showMeshes(RenderingState const& core, Application& app)
{
    ImGui::BeginChild("Meshes", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& mesh : app.meshes.meshes)
    {
        showMeshTree(mesh.second, app.models, (mesh.second.name + ": " + std::to_string(mesh.second.id).c_str()));
    }

    ImGui::EndChild();

    if (ImGui::Button("Create mesh"))
    {
        ImGui::OpenPopup("create_mesh");
    }
    createMesh(core, app);
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
void showObject(Object& obj, Application const& app)
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
        ImGui::Text("Texture");
        ImGui::DragInt("Texture", &obj.texture_index, 1, 0, app.textures.textures.size());
        ImGui::EndPopup();
    }
}
void showScene(Application& app, Scene& scene, Models& models)
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
                showObject(obj, app);
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

    if (ImGui::Button("Create object"))
    {
        ImGui::OpenPopup("create_object");
    }

    ImGui::Text("Terrain");
    ImGui::InputInt("Displacement map", &app.scene.terrain.displacement_map);
    ImGui::InputInt("Normal map", &app.scene.terrain.normal_map);
    ImGui::InputInt("Texture id", &app.scene.terrain.texture_id);
    ImGui::DragFloat("Height", &app.scene.terrain.max_height, 1.0f, 0.0f, 200.0f);


    createObject(app);
}

void showMaterial(Application& application)
{
    ImGui::BeginChild("Materials", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& material : application.programs)
    {
        ImGui::Text(std::to_string(material->id).c_str());
    }

    ImGui::EndChild();

}

void showTextures(Application& application)
{
    ImGui::BeginChild("Textures", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& texture : application.textures.textures)
    {
    }

    ImGui::EndChild();

}

void createGui(RenderingState const& core, Application& application)
{
    ImGui::Begin("Vulkan rendering engine", nullptr, ImGuiWindowFlags_MenuBar);
    if (ImGui::CollapsingHeader("Scene"))
    {
        showScene(application, application.scene, application.models);
    }
    if (ImGui::CollapsingHeader("Meshes"))
    {
        showMeshes(core, application);
    }
    if (ImGui::CollapsingHeader("Models"))
    {
        showModels(core, application.models);
    }
    if (ImGui::CollapsingHeader("Materials"))
    {
        showMaterial(application);
    }
    if (ImGui::CollapsingHeader("Textures"))
    {
        showTextures(application);
    }

    ImGui::End();

}

}
