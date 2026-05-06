#include "Gui.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

#include <array>
#include <chrono>
#include <vector>
#include <string>
#include <iostream>

#include "Material.h"
#include "Scene.h"
#include "height_map.h"
#include "Physics.h"
#include "Maneuver.h"

#include "Application.h"
#include <spdlog/spdlog.h>

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
        static int texture_size = 8;
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
                Model m = createFlatGround(boxes_per_row, size, texture_size);
                m.path = name.data();
                models.models.insert({m.id, m});
            }

            item_current = 0;
            size = 1;
            boxes_per_row = 1;
            texture_size = 8;
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

void comboMeshes(Application& app, int& current_id)
{
}

template<typename T, typename GetLabel, typename Inx>
void ComboBoxName(std::vector<T> const& programs, char const label[], Inx& current_inx, GetLabel c)
{
    if (ImGui::BeginCombo(label, c(programs[current_inx])))
    {
        for (size_t i = 0; i < programs.size(); ++i)
        {
            bool selected = i == current_inx;
            if (ImGui::Selectable(c(programs[i]), &selected))
            {
                current_inx = static_cast<Inx>(i);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void createMaterial(Application& app)
{
    // TODO: Support for creating material
    /*
    if (ImGui::BeginPopup("create_material"))
    {
        static const std::vector<std::string> modes{"Phong", "PBR"};

        static Material i{};

        ImGui::InputText("Name", i.name.data(), i.name.size()-1);

        ComboBoxName(app.programs, "Program", i.program, [](auto& program){return program->name.c_str();});

        ImGui::Checkbox("Has Displacement", &i.has_displacement);
        if (i.has_displacement)
        {
            ComboBoxName(app.textures.textures, "Displacement texture", i.displacement_map_texture, [](auto& text){return text.name.c_str();});
            ComboBoxName(app.textures.textures, "Normal map texture", i.displacement_map_texture, [](auto& text){return text.name.c_str();});
            ImGui::InputFloat("Displacement Y", &i.displacement_y, 0.5f, 2.0f);
        }

        ComboBoxName(modes, "Shading Mode", i.mode, [](auto& text){return text.c_str();});

        if (i.mode == 0)
        {
            ImGui::SliderFloat("Shininess", &i.shininess, 0.0f, 1.0f);
            ImGui::SliderFloat("Specular Strength", &i.specular_strength, 0.0f, 1.0f);
        }
        if (i.mode == 1)
        {
            ImGui::Checkbox("Roughness texture", &i.has_rougness_tex);
            if (i.has_rougness_tex)
            {
                ComboBoxName(app.textures.textures, "Roughness textures", i.roughness_texture, [](auto& text){return text.name.c_str();});
            }
            else
            {
            
                ImGui::SliderFloat("Roughness", &i.roughness, 0.0f, 1.0f);
            }

            ImGui::Checkbox("Metallic texture", &i.has_metallic_tex);
            if (i.has_metallic_tex)
            {
                ComboBoxName(app.textures.textures, "Metallic textures", i.metallic_texture, [](auto& text){return text.name.c_str();});
            }
            else
            {
            
                ImGui::SliderFloat("Metallic", &i.metallic, 0.0f, 1.0f);
            }

            ImGui::Checkbox("AO Texture", &i.has_ao_tex);
            if (i.has_ao_tex)
            {
                ComboBoxName(app.textures.textures, "AO textures",i.ao_texture, [](auto& text){return text.name.c_str();});
            }
            else
            {

                ImGui::SliderFloat("AO", &i.ao, 0.0f, 1.0f);
            }
        }

        ComboBoxName(app.textures.textures, "Color texture", i.base_color_texture, [](auto& text){return text.name.c_str();});
        ComboBoxName(app.textures.textures, "Normal texture", i.base_color_normal_texture, [](auto& text){return text.name.c_str();});
        ImGui::DragFloat("Textures scale", &i.scaling_factor, 0.1, 0.1, 10.0f);

        if (ImGui::Button("Create Material"))
        {
            app.materials.push_back(i);
            i = Material{};
        }

        ImGui::EndPopup();
    }
    */
}

void createObject(Application& app)
{
    /*
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
        if (texture_id < app.textures.textures.size())
        {
            ImGui::Text("Texture: %s", app.textures.textures[texture_id].name.c_str());
        }
        ImGui::DragInt("Material: ", &material, 1, 0, app.programs.size()-1);
        // TODO more info like material, texture, etc

        if (ImGui::Button("Create object"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    */

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

        ImGui::Text("Pos: x:%.1f, y:%.1f, z:%.1f km",camera.pos_d.x,
                                                     camera.pos_d.y,
                                                     camera.pos_d.z);
        ImGui::EndPopup();
    }
}

void showLight(LightBufferObject& light, Scene& scene)
{
    if (ImGui::BeginPopup("light"))
    {
        ImGui::Text("Position");
        ImGui::DragFloat("Pos x", &light.position.x, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos y", &light.position.y, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos z", &light.position.z, 0.1f, -100.0f, 100);

        static float drag = 2.4f;
        if (ImGui::DragFloat("sin/cos", &drag, 0.01,0,4000))
        {
            light.position.x = std::cos(drag)*3000;
            light.position.y = std::sin(drag)*3000;
        }

        static float phi = 0.0f;
        static float theta = glm::pi<float>() * 0.5f;
        bool phi_changed = ImGui::DragFloat("Sun vertical angle", &phi, 0.001f, -glm::pi<float>(), glm::pi<float>());
        bool theta_changed = ImGui::DragFloat("Sun horizontal angle", &theta, 0.001f, -0.5 * glm::pi<float>(), 0.5*glm::pi<float>());
        if (phi_changed || theta_changed)
        {
            glm::vec3 sun_sphere;
            sun_sphere.x = scene.atmosphere.sun_distance * glm::cos(phi);
            sun_sphere.y = scene.atmosphere.sun_distance * glm::sin(phi) * glm::sin(theta);
            sun_sphere.z = scene.atmosphere.sun_distance * glm::sin(phi) * glm::cos(theta);
            light.sun_pos = sun_sphere;
        }

        ImGui::DragFloat("Mie Coefficient", &scene.atmosphere.mie_coefficient, 0.001f, 0.0, 1.0);
        ImGui::DragFloat("Rayleight scatter", &scene.atmosphere.rayleigh_scatter, 0.001, 0.1, 6.0);
        ImGui::DragFloat("Turbidity", &scene.atmosphere.turbidity, 0.1, 150.0f, 0.1f);
        ImGui::DragFloat("Mie Scattering dir", &scene.atmosphere.mie_scattering_dir, 0.001, -0.99, 0.99);
        ImGui::SliderFloat("Sun exposure", &scene.atmosphere.sun_exposure, 0, 5000);
        ImGui::DragFloat("Luminance", &scene.atmosphere.luminance, 0.01, 0.1, 1.2);
        ImGui::SliderFloat("Sun Distance", &scene.atmosphere.sun_distance, 100, 1000000);

        ImGui::Text("Sun Dir");
        ImGui::DragFloat("Pos x", &light.sun_pos.x, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos y", &light.sun_pos.y, 0.1f, -100.0f, 100);
        ImGui::DragFloat("Pos z", &light.sun_pos.z, 0.1f, -100.0f, 100);

/*
        static float drag_sun = 2.4f;
        if (ImGui::DragFloat("sun sin/cos", &drag_sun, 0.01,0,4000))
        {
            light.sun_dir.x = std::cos(drag_sun);
            light.sun_dir.y = std::sin(drag_sun);
        }
*/

        ImGui::Text("Strength");
        ImGui::DragFloat("Strength", &light.strength, 1.0f, 0.0f, 1000.0f);
        static float time_of_the_day = 0.5f;
        if (ImGui::DragFloat("Time of the day", &time_of_the_day, 0.01f, 0.0f, 1.0f))
        {
            spdlog::info("Dragging");
            light.time_of_the_day = time_of_the_day;
        }

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

void showObject(Object& obj, Application& app)
{
    if (ImGui::BeginPopup("object"))
    {
        /*
        static int mesh_id = obj.mesh.id;

        for (auto const& [key, value] : app.meshes.meshes)
        {
            if (ImGui::Button(value.name.c_str()))
            {
                mesh_id = key;
            }
        }

        if (mesh_id != obj.mesh.id)
        {
            obj.mesh = app.meshes.meshes[mesh_id];
        }
        */

        ImGui::Text("Position");
        ImGui::DragFloat("Pos x", &obj.position.x, 0.1f, -100.0f, 100.0f);
        ImGui::DragFloat("Pos y", &obj.position.y, 0.1f, -100.0f, 100.0f);
        ImGui::DragFloat("Pos z", &obj.position.z, 0.1f, -100.0f, 100.0f);
        ImGui::Text("Rotation");
        ImGui::DragFloat("Scale", &obj.scale, 0.01f, 0.01f, 100.0f);
        ImGui::InputFloat("X", &obj.rotation.x, 1.0f, 10.0f);
        ImGui::InputFloat("Y", &obj.rotation.y, 1.0f, 10.0f);
        ImGui::InputFloat("Z", &obj.rotation.z, 1.0f, 10.0f);
        ImGui::Text("Angle");
        ImGui::DragFloat("Angle", &obj.angel, 1, 0, 360);

        auto& i = obj.material.shader_data;
        static const std::vector<std::string> modes{"Phong", "PBR"};

        bool has_displacement = i.material_features & MaterialFeatureFlag::DisplacementMap;
        if (ImGui::Checkbox("Has Displacement", &has_displacement))
        {
            if (has_displacement)
            {
                i.material_features |= MaterialFeatureFlag::DisplacementMap;
            }
            else
            {
                i.material_features &= ~MaterialFeatureFlag::DisplacementMap;
            }
        }

        if (has_displacement)
        {
            ComboBoxName(app.textures.textures, "Displacement texture", i.displacement_map_texture, [](auto& text){return text->name.c_str();});
            ComboBoxName(app.textures.textures, "Normal map texture", i.normal_map_texture, [](auto& text){return text->name.c_str();});
            ImGui::InputFloat("Displacement Y", &i.displacement_y, 0.5f, 2.0f);
        }

        static const std::vector<std::string> sampling_mode{"Triplanar", "UV"};
        ComboBoxName(sampling_mode, "Sampling Mode", i.sampling_mode, [](auto& text){return text.c_str();});

        ComboBoxName(modes, "Shading Mode", i.shade_mode, [](auto& text){return text.c_str();});

        if (i.shade_mode == ReflectionShadeMode::Phong)
        {
            ImGui::DragFloat("Shininess", &i.shininess, 0.5f, 2.0f, 256.0f);
            ImGui::DragFloat("Specular Strength", &i.specular_strength, 0.01f, 0.0f, 1.0f);
        }
        if (i.shade_mode == ReflectionShadeMode::Pbr)
        {
            bool has_roughness = i.material_features & MaterialFeatureFlag::RoughnessMap;
            if (ImGui::Checkbox("Has Roughness", &has_roughness))
            {
                if (has_roughness)
                {
                    i.material_features |= MaterialFeatureFlag::RoughnessMap;
                }
                else
                {
                    i.material_features &= ~MaterialFeatureFlag::RoughnessMap;
                }
            }
            if (has_roughness)
            {
                ComboBoxName(app.textures.textures, "Roughness textures", i.roughness_texture, [](auto& text){return text->name.c_str();});
            }
            else
            {
            
                ImGui::SliderFloat("Roughness", &i.roughness, 0.0f, 1.0f);
            }




            bool has_metalness = i.material_features & MaterialFeatureFlag::MetalnessMap;
            if (ImGui::Checkbox("Has Metalness", &has_metalness))
            {
                if (has_metalness)
                {
                    i.material_features |= MaterialFeatureFlag::MetalnessMap;
                }
                else
                {
                    i.material_features &= ~MaterialFeatureFlag::MetalnessMap;
                }
            }
            if (has_metalness)
            {
                ComboBoxName(app.textures.textures, "Metallic textures", i.metallic_texture, [](auto& text){return text->name.c_str();});
            }
            else
            {
            
                ImGui::SliderFloat("Metallic", &i.metallic, 0.0f, 1.0f);
            }



            bool has_ao = i.material_features & MaterialFeatureFlag::AoMap;
            if (ImGui::Checkbox("Has AO", &has_ao))
            {
                if (has_ao)
                {
                    i.material_features |= MaterialFeatureFlag::AoMap;
                }
                else
                {
                    i.material_features &= ~MaterialFeatureFlag::AoMap;
                }
            }
            if (has_ao)
            {
                ComboBoxName(app.textures.textures, "AO textures",i.ao_texture, [](auto& text){return text->name.c_str();});
            }
            else
            {

                ImGui::SliderFloat("AO", &i.ao, 0.0f, 1.0f);
            }
        }

        ComboBoxName(app.textures.textures, "Color texture", i.base_color_texture, [](auto& text){return text->name.c_str();});
        ComboBoxName(app.textures.textures, "Normal texture", i.base_color_normal_texture, [](auto& text){return text->name.c_str();});
        ImGui::DragFloat("Textures scale", &i.scaling_factor, 0.1, 0.1, 10.0f);
        ImGui::EndPopup();
    }
}
void showScene(Application& app, Scene& scene, Models& models)
{
    ImGui::BeginChild("Scene", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::TreeNode("Camera"))
    {
        Camera& camera = scene.camera;
        ImGui::Text("Pos: x:%.1f, y:%.1f, z:%.1f km", camera.pos_d.x,
                                                       camera.pos_d.y,
                                                       camera.pos_d.z);
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

        showLight(light, scene);
        ImGui::TreePop();
    }

    int index = 0;
    for (auto& obj: scene.objs)
    {
        if (ImGui::TreeNode(std::to_string(index++).c_str()))
        {
            showObject(obj, app);
            ImGui::Text("Pos: x:%f, y:%f, z:%f",obj.position.x, obj.position.y, obj.position.z);
            ImGui::Text("Angle: %f", obj.angel);
            ImGui::Text("Scale: %f", obj.scale);
            // ImGui::Text("Indices: %d", obj.mesh.indices_size);
            ImGui::Text("Material: %s", obj.material.name.data());
            // showMeshTree(obj.mesh, models, std::string("Mesh: ") + obj.mesh.name + " " + std::to_string(obj.mesh.id));
            if (ImGui::Button("Edit"))
            {
                ImGui::OpenPopup("object");
            }

            /*
            int material = obj.material;
            ImGui::InputInt("Change Material", &material);
            if (material != obj.material && material < scene.materials.size())
            {
                changeMaterial(scene, index-1, material);
            }
            */
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();

    if (ImGui::Button("Create object"))
    {
        ImGui::OpenPopup("create_object");
    }
    if (ImGui::Button("Create material"))
    {
        ImGui::OpenPopup("create_material");
    }
    bool hdr_resolve = app.ppp.buffer_object.hdr_resolve == 1;
    if (ImGui::Checkbox("Rersolve HDR", &hdr_resolve))
    {
        app.ppp.buffer_object.hdr_resolve = hdr_resolve ? 1 : 0;
    }

    ImGui::Text("Terrain");
    ImGui::InputInt("Displacement map", &app.scene.terrain.displacement_map);
    ImGui::InputInt("Normal map", &app.scene.terrain.normal_map);
    ImGui::InputInt("Texture id", &app.scene.terrain.texture_id);
    ImGui::InputInt("Texture normal", &app.scene.terrain.texture_normal_map);
    ImGui::DragFloat("Blend sharpness", &app.scene.terrain.blend_sharpness, 1.0f, 0.01f, 100.0f);
    ImGui::DragFloat("Shininess", &app.scene.terrain.shininess, 0.1f, 0.00f, 100.0f);
    ImGui::DragFloat("Specular strength", &app.scene.terrain.specular_strength, 0.1f, 0.0f, 100.0f);
    ImGui::DragFloat("Metalness", &app.scene.terrain.metalness, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Roughness", &app.scene.terrain.roughness, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("ao", &app.scene.terrain.ao, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Texture scale", &app.scene.terrain.texture_scale, 0.01f, 0.001, 100);
    ImGui::DragFloat("Height", &app.scene.terrain.max_height, 1.0f, 0.0f, 200.0f);
    ImGui::DragFloat("Lod min", &app.scene.terrain.lod_min, 1.0, 0, app.scene.terrain.lod_max);
    ImGui::DragFloat("Lod max", &app.scene.terrain.lod_max, 1.0f, app.scene.terrain.lod_min, 11.0f);
    ImGui::DragFloat("Lod weight", &app.scene.terrain.weight, 1.0f, 0, 2000);

    ImGui::Text("Fog");
    bool fog_enabled = scene.fog.volumetric_fog_enabled == 1;
    if (ImGui::Checkbox("Volumetric Fog Enabled", &fog_enabled))
    {
        scene.fog.volumetric_fog_enabled = fog_enabled ? 1 : 0;
    }

    ImGui::DragFloat("Fog Density", &app.scene.fog.base_density, 0.01f, 0.00f, 100.0f);
    ImGui::DragFloat("Turbulence", &app.scene.fog.turbulence, 0.00001f, 0.00f, 100.0f);
    ImGui::DragFloat("Wind", &app.scene.fog.wind, 0.01f, 0.00f, 100.0f);
    ImGui::DragFloat("Time", &app.scene.fog.time, 0.01f, 0.00f, 100.0f);

    static float fog_color[3] {scene.fog.color.r, scene.fog.color.g, scene.fog.color.b};
    if (ImGui::ColorEdit3("Color", fog_color))
    {
        scene.fog.color.r = fog_color[0];
        scene.fog.color.g = fog_color[1];
        scene.fog.color.b = fog_color[2];
    }

    ImGui::DragFloat("Max Density", &app.scene.fog.max_density, 0.01f, 0.00f, 1.0f);


    createObject(app);
    createMaterial(app);
}

void showMaterial(Application& application)
{
    ImGui::BeginChild("Materials", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& material : application.materials)
    {
        ImGui::Text("Name: %s", material.name.data());
    }

    ImGui::EndChild();

}

void showTextures(Application& application)
{
    ImGui::BeginChild("Textures", ImVec2(-1, 300), true, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& texture : application.textures.textures)
    {
        ImGui::Text("%s", texture->name.c_str());
    }

    ImGui::EndChild();

}

void createSolarSystemGui(SolarSystem& solar_system, Camera& cam)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 460), ImGuiCond_FirstUseEver);
    ImGui::Begin("Solar System");

    ImGui::Text("Camera: %.1f, %.1f, %.1f km", cam.pos_d.x, cam.pos_d.y, cam.pos_d.z);

    if (solar_system.paused)
    {
        if (ImGui::Button("Resume")) solar_system.paused = false;
    }
    else
    {
        if (ImGui::Button("Pause"))  solar_system.paused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Top View"))
    {
        cam.orbit_elevation = 89.0f;
        cam.orbit_azimuth   = 0.0f;
        updateCameraFromOrbit(cam);
    }
    ImGui::SameLine();
    if (ImGui::Button("Side View"))
    {
        cam.orbit_elevation = 0.0f;
        cam.orbit_azimuth   = 0.0f;
        updateCameraFromOrbit(cam);
    }

    {
        int mode = static_cast<int>(solar_system.physics_mode);
        ImGui::RadioButton("N-body", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Patched Conic", &mode, 1);
        solar_system.physics_mode = static_cast<PhysicsMode>(mode);
    }

    ImGui::Separator();

    // Simulation date — J2000 epoch + elapsed seconds
    {
        using namespace std::chrono;
        auto epoch   = sys_days{year{2000}/January/1};
        auto elapsed = duration_cast<seconds>(duration<double>(solar_system.elapsed_simulation_s));
        auto now     = epoch + elapsed;
        auto dp      = floor<days>(now);
        year_month_day ymd{dp};
        hh_mm_ss       hms{now - dp};

        ImGui::Text("Date: %04d-%02u-%02u  %02lld:%02lld:%02lld",
            (int)ymd.year(),
            (unsigned)ymd.month(),
            (unsigned)ymd.day(),
            (long long)hms.hours().count(),
            (long long)hms.minutes().count(),
            (long long)hms.seconds().count());
    }

    static double ts_min = 1.0, ts_max = 1'000'000.0;
    ImGui::SliderScalar("Time scale", ImGuiDataType_Double, &solar_system.time_scale, &ts_min, &ts_max, "%.0f x",
                        ImGuiSliderFlags_Logarithmic);

    // Object selection
    {
        // Build combo label
        const char* selected_name =
            (solar_system.selected_body < 0)
            ? "Sun"
            : solar_system.defs[solar_system.selected_body].name;

        if (ImGui::BeginCombo("Focus", selected_name))
        {
            if (ImGui::Selectable("Sun", solar_system.selected_body == -1))
            {
                solar_system.selected_body = -1;
                solar_system.selected_moon = -1;
            }
            for (int i = 0; i < (int)solar_system.defs.size(); ++i)
            {
                bool selected = (solar_system.selected_body == i);
                if (ImGui::Selectable(solar_system.defs[i].name, selected))
                {
                    solar_system.selected_body = i;
                    solar_system.selected_moon = -1;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Separator();

    // Orbit ring controls
    ImGui::Checkbox("Show orbit rings", &solar_system.show_orbits);
    if (solar_system.show_orbits)
    {
        ImGui::SameLine();
        ImGui::Checkbox("Stippled##orb", &solar_system.orbit_stippled);
        ImGui::SliderFloat("Orbit width",   &solar_system.orbit_line_width, 0.5f, 8.0f);
        ImGui::SliderFloat("Orbit opacity", &solar_system.orbit_opacity,    0.0f, 1.0f);
    }

    // Grid controls
    ImGui::Checkbox("Show grid", &solar_system.show_grid);
    if (solar_system.show_grid)
    {
        ImGui::SliderFloat("Grid width",   &solar_system.grid_line_width, 0.5f, 4.0f);
        ImGui::SliderFloat("Grid opacity", &solar_system.grid_opacity,    0.0f, 1.0f);
        ImGui::SliderFloat("Grid spacing (Mkm)", &solar_system.grid_spacing_km,
                           1e7f, 2e9f, "%.3e");
    }

    ImGui::TextDisabled("Scroll: zoom  |  L-drag: orbit  |  R-drag: pan");

    ImGui::Separator();
    ImGui::Text("  Label  %-8s  Distance", "Body");
    ImGui::Separator();

    int moon_flat_idx = 0;
    for (int i = 0; i < (int)solar_system.defs.size(); ++i)
    {
        auto const& def = solar_system.defs[i];
        double dist = glm::length(interpolatedPosition(solar_system, i) - cam.pos_d);
        bool planet_selected = (solar_system.selected_body == i && solar_system.selected_moon < 0);
        bool has_moons = !def.moons.empty();

        ImGui::PushID(i);

        if (has_moons)
        {
            bool open = ImGui::TreeNodeEx("##tree", ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine();
            bool show = solar_system.show_label[i];
            if (ImGui::Checkbox("##lbl", &show)) solar_system.show_label[i] = show;
            ImGui::SameLine();
            if (planet_selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
            char planet_buf[80];
            std::snprintf(planet_buf, sizeof(planet_buf), "%-8s  %12.0f km##planet%d", def.name, dist, i);
            if (ImGui::Selectable(planet_buf, planet_selected))
            {
                solar_system.selected_body = i;
                solar_system.selected_moon = -1;
            }
            if (planet_selected) ImGui::PopStyleColor();

            if (open)
            {
                for (int j = 0; j < (int)def.moons.size(); ++j, ++moon_flat_idx)
                {
                    auto const& moon_def = def.moons[j];
                    double moon_dist = glm::length(interpolatedMoonPosition(solar_system, moon_flat_idx) - cam.pos_d);
                    bool moon_selected = (solar_system.selected_moon == moon_flat_idx);

                    ImGui::PushID(moon_flat_idx + 10000);
                    bool moon_show = solar_system.show_moon_label[moon_flat_idx];
                    if (ImGui::Checkbox("##mlbl", &moon_show)) solar_system.show_moon_label[moon_flat_idx] = moon_show;
                    ImGui::SameLine();
                    if (moon_selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
                    char moon_buf[80];
                    std::snprintf(moon_buf, sizeof(moon_buf), "  %-6s  %12.0f km##moon%d", moon_def.name, moon_dist, moon_flat_idx);
                    if (ImGui::Selectable(moon_buf, moon_selected))
                    {
                        solar_system.selected_moon        = moon_flat_idx;
                        solar_system.selected_body        = -1;
                        solar_system.selected_spacecraft  = -1;
                    }
                    if (moon_selected) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            else
            {
                moon_flat_idx += static_cast<int>(def.moons.size());
            }
        }
        else
        {
            bool show = solar_system.show_label[i];
            if (ImGui::Checkbox("##lbl", &show)) solar_system.show_label[i] = show;
            ImGui::SameLine();
            if (planet_selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
            char planet_buf[80];
            std::snprintf(planet_buf, sizeof(planet_buf), "%-8s  %12.0f km##planet%d", def.name, dist, i);
            if (ImGui::Selectable(planet_buf, planet_selected))
            {
                solar_system.selected_body = i;
                solar_system.selected_moon = -1;
            }
            if (planet_selected) ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void drawPlanetLabels(SolarSystem const& solar_system, Camera const& cam)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;

    // Recompute view matrix (CRR: eye is always at origin)
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f), cam.camera_front, cam.up);

    for (int i = 0; i < (int)solar_system.defs.size(); ++i)
    {
        if (!solar_system.show_label[i]) continue;

        auto const& def   = solar_system.defs[i];
        auto const& state = solar_system.states[i];

        // Camera-relative world position — use interpolated pos so the label
        // tracks the sphere exactly rather than jumping on each physics step.
        glm::vec3 cam_rel = glm::vec3(interpolatedPosition(solar_system, i) - cam.pos_d);

        glm::vec4 clip = cam.proj * view * glm::vec4(cam_rel, 1.0f);

        if (clip.w <= 0.0f) continue; // behind camera

        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) continue;

        float sx = ( ndc.x * 0.5f + 0.5f) * screen.x;
        float sy = (-ndc.y * 0.5f + 0.5f) * screen.y;

        // Shadow for readability
        dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 200), def.name);
        dl->AddText(ImVec2(sx,     sy    ), IM_COL32(255, 255, 100, 255), def.name);

        // Small crosshair dot
        dl->AddCircleFilled(ImVec2(sx, sy - 8), 3.0f, IM_COL32(255, 255, 100, 200));
    }

    for (int k = 0; k < (int)solar_system.moon_states.size(); ++k)
    {
        if (!solar_system.show_moon_label[k]) continue;

        auto const& moon_state = solar_system.moon_states[k];
        auto const& moon_def   = solar_system.defs[moon_state.parent_planet_index].moons[moon_state.moon_index];

        glm::vec3 cam_rel = glm::vec3(interpolatedMoonPosition(solar_system, k) - cam.pos_d);
        glm::vec4 clip    = cam.proj * view * glm::vec4(cam_rel, 1.0f);

        if (clip.w <= 0.0f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) continue;

        float sx = ( ndc.x * 0.5f + 0.5f) * screen.x;
        float sy = (-ndc.y * 0.5f + 0.5f) * screen.y;

        dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 200), moon_def.name);
        dl->AddText(ImVec2(sx,     sy    ), IM_COL32(200, 200, 255, 255), moon_def.name);
        dl->AddCircleFilled(ImVec2(sx, sy - 8), 3.0f, IM_COL32(200, 200, 255, 200));
    }

    // Spacecraft labels
    for (int j = 0; j < static_cast<int>(solar_system.spacecraft_states.size()); ++j)
    {
        if (j >= static_cast<int>(solar_system.show_spacecraft_label.size())) break;
        if (!solar_system.show_spacecraft_label[j]) continue;

        auto const& sc  = solar_system.spacecraft_states[j];
        auto const& def = solar_system.spacecraft_defs[j];

        glm::vec3 cam_rel = glm::vec3(interpolatedSpacecraftPosition(solar_system, j) - cam.pos_d);
        glm::vec4 clip    = cam.proj * view * glm::vec4(cam_rel, 1.0f);
        if (clip.w <= 0.0f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) continue;

        float sx = ( ndc.x * 0.5f + 0.5f) * screen.x;
        float sy = (-ndc.y * 0.5f + 0.5f) * screen.y;

        dl->AddText(ImVec2(sx + 1, sy + 1), IM_COL32(0, 0, 0, 200), def.name);
        dl->AddText(ImVec2(sx,     sy    ), IM_COL32(255, 220, 80, 255), def.name);
        dl->AddCircleFilled(ImVec2(sx, sy - 8), 3.0f, IM_COL32(255, 220, 80, 200));
    }
}


void createSpacecraftGui(SolarSystem& ss, Camera& cam)
{
    ImGui::SetNextWindowPos(ImVec2(10, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Spacecraft Control");

    if (ImGui::Button("Spawn at Earth"))
    {
        spawnSpacecraftAtEarth(ss.spacecraft_defs, ss.spacecraft_states, ss);
        ss.show_spacecraft_label.push_back(true); // label on by default
        ss.selected_spacecraft = static_cast<int>(ss.spacecraft_states.size()) - 1;
    }

    if (ss.spacecraft_defs.empty())
    {
        ImGui::TextDisabled("No spacecraft. Press 'Spawn at Earth' to add one.");
        ImGui::End();
        return;
    }

    ImGui::SameLine();
    if (ImGui::Button("Focus Camera") && ss.selected_spacecraft >= 0)
    {
        cam.orbit_distance  = ss.spacecraft_defs[ss.selected_spacecraft].visual_scale_km * 8.0;
        ss.selected_body    = -1;   // release planet/sun focus so camera follows spacecraft
        ss.selected_moon    = -1;
    }

    // Spacecraft selector with per-craft label toggle
    {
        const char* sel_name = (ss.selected_spacecraft >= 0)
            ? ss.spacecraft_defs[ss.selected_spacecraft].name
            : "None";
        if (ImGui::BeginCombo("Select", sel_name))
        {
            for (int i = 0; i < static_cast<int>(ss.spacecraft_defs.size()); ++i)
            {
                bool selected = (ss.selected_spacecraft == i);
                if (i < static_cast<int>(ss.show_spacecraft_label.size()))
                {
                    ImGui::PushID(i);
                    bool lbl = ss.show_spacecraft_label[i];
                    if (ImGui::Checkbox("##lbl", &lbl)) ss.show_spacecraft_label[i] = lbl;
                    ImGui::SameLine();
                    ImGui::PopID();
                }
                if (ImGui::Selectable(ss.spacecraft_defs[i].name, selected))
                    ss.selected_spacecraft = i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    {
        bool pg = ss.spacecraft_follow_orbit;
        bool rg = ss.spacecraft_follow_orbit_retrograde;
        if (ImGui::Checkbox("Prograde", &pg))
        {
            ss.spacecraft_follow_orbit            = pg;
            ss.spacecraft_follow_orbit_retrograde = false;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Retrograde", &rg))
        {
            ss.spacecraft_follow_orbit_retrograde = rg;
            ss.spacecraft_follow_orbit            = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(auto-align)");
    }
    if (!ss.spacecraft_follow_orbit && !ss.spacecraft_follow_orbit_retrograde)
        ImGui::SliderFloat("Rotation rate (deg/s)", &ss.spacecraft_rotation_rate, 5.0f, 180.0f);

    if (ss.selected_spacecraft < 0 ||
        ss.selected_spacecraft >= static_cast<int>(ss.spacecraft_states.size()))
    {
        ImGui::End();
        return;
    }

    auto&       sc  = ss.spacecraft_states[ss.selected_spacecraft];
    auto const& def = ss.spacecraft_defs[ss.selected_spacecraft];

    ImGui::Separator();

    // Position
    ImGui::Text("Position (km)");
    ImGui::Text("  X: %+.3e   Y: %+.3e   Z: %+.3e",
        sc.position_km.x, sc.position_km.y, sc.position_km.z);

    // SOI + orbital information
    {
        constexpr double G_KM3_GUI  = 6.674e-20;
        constexpr double GM_SUN_GUI = 1.32712440018e11;
        std::size_t const sci = static_cast<std::size_t>(ss.selected_spacecraft);

        // Determine dominant body name, position, velocity, radius, GM
        const char* dom_name = "Sun";
        glm::dvec3  dom_pos{0.0};
        glm::dvec3  dom_vel{0.0};
        double      dom_radius_km = ss.defs[0].radius_km;
        double      dom_GM        = GM_SUN_GUI;

        if (sc.dominant_is_moon && sc.dominant_moon_idx >= 0 &&
            sc.dominant_moon_idx < static_cast<int>(ss.moon_states.size()))
        {
            std::size_t const mk = static_cast<std::size_t>(sc.dominant_moon_idx);
            auto const& ms       = ss.moon_states[mk];
            auto const& mdef     = ss.defs[ms.parent_planet_index].moons[ms.moon_index];
            dom_name      = mdef.name;
            dom_pos       = interpolatedMoonPosition(ss, mk);
            dom_vel       = interpolatedMoonVelocity(ss, mk);
            dom_radius_km = mdef.radius_km;
            dom_GM        = G_KM3_GUI * mdef.mass_kg;
        }
        else if (sc.dominant_body_idx > 0 &&
                 sc.dominant_body_idx < static_cast<int>(ss.defs.size()))
        {
            std::size_t const bi = static_cast<std::size_t>(sc.dominant_body_idx);
            dom_name      = ss.defs[bi].name;
            dom_pos       = interpolatedPosition(ss, bi);
            dom_vel       = interpolatedVelocity(ss, bi);
            dom_radius_km = ss.defs[bi].radius_km;
            dom_GM        = G_KM3_GUI * ss.defs[bi].mass_kg;
        }

        glm::dvec3 const sc_pos = interpolatedSpacecraftPosition(ss, sci);
        double const dist_km    = glm::length(sc_pos - dom_pos);
        double const alt_km     = dist_km - dom_radius_km;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "SOI: %s", dom_name);
        ImGui::Text("Distance: %.1f km", dist_km);
        ImGui::Text("Altitude: %.1f km", alt_km);

        // Osculating orbit relative to dominant body
        glm::dvec3 const r_rel = sc.position_km - dom_pos;
        glm::dvec3 const v_rel = sc.velocity_km  - dom_vel;
        if (glm::length(r_rel) > 1e-6 && glm::length(v_rel) > 1e-12)
        {
            OsculatingOrbit const orb = computeOsculatingOrbit(r_rel, v_rel, dom_GM);
            ImGui::Separator();
            if (orb.e < 1.0 && orb.a > 0.0)
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Elliptic orbit");
                double const peri_alt = orb.periapsis_km() - dom_radius_km;
                double const apo_alt  = orb.apoapsis_km()  - dom_radius_km;
                ImGui::Text("Eccentricity: %.4f", orb.e);
                ImGui::Text("Semi-major axis: %.1f km", orb.a);
                ImGui::Text("Periapsis alt: %.1f km", peri_alt);
                ImGui::Text("Apoapsis alt:  %.1f km", apo_alt);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Hyperbolic - not captured");
                double const peri_km  = orb.periapsis_km();
                double const peri_alt = peri_km - dom_radius_km;
                ImGui::Text("Eccentricity: %.4f", orb.e);
                ImGui::Text("Closest approach: %.1f km alt", peri_alt);
            }
        }
        ImGui::Separator();
    }

    // Velocity
    double v_mag = glm::length(sc.velocity_km);
    ImGui::Text("Velocity (km/s)");
    ImGui::Text("  X: %+.3f   Y: %+.3f   Z: %+.3f   |v|: %.3f",
        sc.velocity_km.x, sc.velocity_km.y, sc.velocity_km.z, v_mag);

    // Orientation — show nose (forward) direction
    glm::vec3 nose = glm::mat3_cast(glm::quat(sc.orientation)) * glm::vec3(0.0f, 1.0f, 0.0f);
    ImGui::Text("Nose direction");
    ImGui::Text("  X: %+.3f   Y: %+.3f   Z: %+.3f", nose.x, nose.y, nose.z);

    // Thrust
    ImGui::Text("Thrust: %.1f%%", sc.thrust_level * 100.0);
    ImGui::SameLine();
    if (ImGui::Button("Stop Thrust"))
    {
        sc.thrust_level              = 0.0;
        ss.spacecraft_path_dirty     = true;
    }
    ImGui::ProgressBar(static_cast<float>(sc.thrust_level), ImVec2(-1, 0));

    ImGui::Separator();
    ImGui::TextDisabled("WASD: pitch/yaw  QE: roll  Z: +thrust  X: -thrust");

    // Orbit ring controls
    ImGui::Separator();
    ImGui::Checkbox("Show orbit ring", &ss.show_spacecraft_orbit);
    if (ss.show_spacecraft_orbit)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("##orb_w", &ss.spacecraft_orbit_line_width, 0.5f, 4.0f, "w:%.1f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderFloat("##orb_a", &ss.spacecraft_orbit_opacity, 0.0f, 1.0f, "a:%.2f");
    }

    // Predicted path controls
    if (ImGui::Checkbox("Show predicted path", &ss.show_spacecraft_path))
        ss.spacecraft_path_dirty = true;
    if (ss.show_spacecraft_path)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        float dur_f = static_cast<float>(ss.spacecraft_path_duration_s);
        if (ImGui::SliderFloat("##path_dur", &dur_f, 1000.0f, 30000.0f, "%.0f s"))
        {
            ss.spacecraft_path_duration_s = dur_f;
            ss.spacecraft_path_dirty      = true;
        }
    }

    // Maneuver planning button
    ImGui::Separator();
    if (ImGui::Button("Plan Maneuver"))
    {
        ss.maneuver_mode                = true;
        ss.maneuver_targets_initialized = false; // let updateSceneFromSolarSystem seed targets
        ss.paused                       = true;
        ss.maneuver_sc_idx              = ss.selected_spacecraft;
        ss.maneuver_t0_s                = 0.0;

        ManeuverNode pending{};
        pending.t0_s = 0.0;
        sc.maneuvers.push_back(pending);
    }

    ImGui::End();
}

static void createManeuverPlannerGui(SolarSystem& ss)
{
    if (!ss.maneuver_mode) return;
    if (ss.maneuver_sc_idx < 0 ||
        ss.maneuver_sc_idx >= static_cast<int>(ss.spacecraft_states.size())) return;

    auto& sc  = ss.spacecraft_states[ss.maneuver_sc_idx];
    auto const& def = ss.spacecraft_defs[ss.maneuver_sc_idx];

    ImGui::SetNextWindowPos(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Maneuver Planner", &open))
    {
        ImGui::Text("Planning maneuver for: %s", def.name);
        ImGui::Separator();

        float t0_min = static_cast<float>(ss.maneuver_t0_s / 60.0);
        if (ImGui::SliderFloat("T0 (sim-min)", &t0_min, 0.0f, 60.0f, "%.1f min"))
            ss.maneuver_t0_s = static_cast<double>(t0_min) * 60.0;

        ImGui::TextDisabled("Reference at T0:  pg %.4f  rd %.4f  nm %.4f  km/s",
            ss.maneuver_ref_prograde, ss.maneuver_ref_radial, ss.maneuver_ref_normal);

        float pg = static_cast<float>(ss.maneuver_prograde);
        float rd = static_cast<float>(ss.maneuver_radial);
        float nm = static_cast<float>(ss.maneuver_normal);
        if (ImGui::DragFloat("Prograde (km/s)", &pg, 0.001f, 0.0f, 15.0f,  "%.4f")) ss.maneuver_prograde = pg;
        if (ImGui::DragFloat("Radial   (km/s)", &rd, 0.001f, -5.0f,  5.0f, "%.4f")) ss.maneuver_radial   = rd;
        if (ImGui::DragFloat("Normal   (km/s)", &nm, 0.001f, -5.0f,  5.0f, "%.4f")) ss.maneuver_normal   = nm;

        // Per-component burn Δv (absolute slider value minus reference at T0)
        double const dpg = ss.maneuver_prograde - ss.maneuver_ref_prograde;
        double const drd = ss.maneuver_radial   - ss.maneuver_ref_radial;
        double const dnm = ss.maneuver_normal   - ss.maneuver_ref_normal;
        ImGui::Text("  Prograde: %+.4f km/s", dpg);
        ImGui::Text("  Radial:   %+.4f km/s", drd);
        ImGui::Text("  Normal:   %+.4f km/s", dnm);

        // Actual |Δv| from the pending node (updated by updateSceneFromSolarSystem before GUI runs)
        double dv_mag = 0.0;
        for (auto const& node : sc.maneuvers)
            if (!node.approved) { dv_mag = glm::length(node.delta_v_world); break; }

        ImGui::Text("Total |Δv|: %.4f km/s", dv_mag);

        double const burn_rate = def.thrust_N / def.mass_kg * 1e-3; // km/s²
        double const burn_time = (burn_rate > 0.0) ? dv_mag / burn_rate : 0.0;
        ImGui::Text("Est. burn time: %.1f s", burn_time);

        ImGui::Separator();
        if (ImGui::Button("Approve"))
        {
            // Find the pending node and approve it
            // delta_v_world, burn_pos_rel, burn_vel_rel are already up-to-date
            // (set by updateSceneFromSolarSystem which runs before the GUI each frame)
            for (auto& node : sc.maneuvers)
            {
                if (!node.approved)
                {
                    node.t0_s        = ss.maneuver_t0_s;
                    node.t0_abs_s    = ss.elapsed_simulation_s + ss.maneuver_t0_s;
                    node.prograde_dv = ss.maneuver_prograde - ss.maneuver_ref_prograde;
                    node.radial_dv   = ss.maneuver_radial   - ss.maneuver_ref_radial;
                    node.normal_dv   = ss.maneuver_normal   - ss.maneuver_ref_normal;
                    node.approved    = true;
                    break;
                }
            }
            ss.maneuver_mode = false;
            ss.paused        = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            // Remove the pending unapproved node
            auto& mnv = sc.maneuvers;
            mnv.erase(std::remove_if(mnv.begin(), mnv.end(),
                          [](ManeuverNode const& n){ return !n.approved; }), mnv.end());
            ss.maneuver_mode = false;
            ss.paused        = false;
        }
    }
    ImGui::End();

    if (!open)
    {
        // Window X-closed = cancel
        auto& mnv = sc.maneuvers;
        mnv.erase(std::remove_if(mnv.begin(), mnv.end(),
                      [](ManeuverNode const& n){ return !n.approved; }), mnv.end());
        ss.maneuver_mode = false;
        ss.paused        = false;
    }
}

static void createActiveManeuversHud(SolarSystem& ss)
{
    // Count active approved maneuvers across all spacecraft
    bool any_active = false;
    for (auto const& sc : ss.spacecraft_states)
        for (auto const& node : sc.maneuvers)
            if (node.approved) { any_active = true; break; }
    if (!any_active) return;

    ImGui::SetNextWindowPos(ImVec2(10, 800), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Active Maneuvers");

    for (int j = 0; j < static_cast<int>(ss.spacecraft_states.size()); ++j)
    {
        auto&       sc  = ss.spacecraft_states[j];
        auto const& def = ss.spacecraft_defs[j];

        for (int ni = static_cast<int>(sc.maneuvers.size()) - 1; ni >= 0; --ni)
        {
            auto& node = sc.maneuvers[ni];
            if (!node.approved) continue;

            ImGui::PushID(j * 100 + ni);

            double t_until = node.t0_abs_s - ss.elapsed_simulation_s;
            double dv_mag  = glm::length(node.delta_v_world);
            bool   burning = (t_until <= 0.0 && sc.thrust_level > 0.0);

            int abs_s = static_cast<int>(std::abs(t_until));
            int hrs   = abs_s / 3600;
            int mins  = (abs_s % 3600) / 60;
            int secs  = abs_s % 60;

            if (node.completed)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[%s]  COMPLETED  |Δv| %.3f km/s",
                    def.name, dv_mag);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, burning
                    ? ImVec4(1.0f, 0.4f, 0.0f, 1.0f)
                    : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::Text("[%s]  T%s%02d:%02d:%02d  |Δv| %.3f km/s",
                    def.name,
                    (t_until >= 0 ? "-" : "+"),
                    hrs, mins, secs,
                    dv_mag);
                ImGui::PopStyleColor();
            }

            ImGui::Text("  pg: %+.3f  rd: %+.3f  nm: %+.3f",
                node.prograde_dv, node.radial_dv, node.normal_dv);

            if (!node.completed)
            {
                if (dv_mag > 0.0 && node.accumulated_dv > 0.0)
                {
                    float prog = static_cast<float>(node.accumulated_dv / dv_mag);
                    ImGui::ProgressBar(prog, ImVec2(-1, 0));
                    ImGui::Text("  Remaining: %.4f km/s", dv_mag - node.accumulated_dv);
                }
                else if (t_until <= 30.0 && t_until > 0.0)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.0f, 1.0f), ">>> BURN IMMINENT <<<");
                }
                ImGui::Checkbox("Lock attitude to burn direction", &node.lock_attitude);
            }

            if (ImGui::Button(node.completed ? "Remove" : "Cancel maneuver"))
                sc.maneuvers.erase(sc.maneuvers.begin() + ni);

            ImGui::Separator();
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void createGui(RenderingState const& core, Application& application, SolarSystem* solar_system)
{
    if (solar_system)
    {
        createSolarSystemGui(*solar_system, application.scene.camera);
        drawPlanetLabels(*solar_system, application.scene.camera);
        createSpacecraftGui(*solar_system, application.scene.camera);
        createManeuverPlannerGui(*solar_system);
        createActiveManeuversHud(*solar_system);
    }

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
