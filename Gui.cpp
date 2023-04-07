#include "Gui.h"

#include <imgui.h>

#include <array>
#include <vector>
#include <string>
#include <iostream>

namespace gui
{

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
        ImGui::InputText("Vertex shader", new_program.vertex_shader.data(), new_program.vertex_shader.size());
        ImGui::InputText("Fragment shader", new_program.fragment_shader.data(), new_program.fragment_shader.size());

    }

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

}
