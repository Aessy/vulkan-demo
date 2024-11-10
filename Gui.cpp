#include "Gui.h"

#include <imgui.h>

#include <array>
#include <vector>
#include <string>
#include <iostream>

#include "Material.h"
#include "Scene.h"
#include "height_map.h"

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

        ComboBoxName(app.programs, "Program", obj.material.program, [](auto& program){return program->name.c_str();});
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
            ComboBoxName(app.textures.textures, "Displacement texture", i.displacement_map_texture, [](auto& text){return text.name.c_str();});
            ComboBoxName(app.textures.textures, "Normal map texture", i.normal_map_texture, [](auto& text){return text.name.c_str();});
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
                ComboBoxName(app.textures.textures, "Roughness textures", i.roughness_texture, [](auto& text){return text.name.c_str();});
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
                ComboBoxName(app.textures.textures, "Metallic textures", i.metallic_texture, [](auto& text){return text.name.c_str();});
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
            ImGui::Text("Indices: %d", obj.mesh.indices_size);
            ImGui::Text("Material: %s", obj.material.name.data());
            showMeshTree(obj.mesh, models, std::string("Mesh: ") + obj.mesh.name + " " + std::to_string(obj.mesh.id));
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
        ImGui::Text("%s", texture.name.c_str());
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
