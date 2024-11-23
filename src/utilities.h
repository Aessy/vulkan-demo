#pragma once

#include "VulkanRenderSystem.h"

#include <vector>
#include <string>
#include <fstream>

inline std::vector<char> readFile(std::string const& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

inline std::vector<vk::Buffer> createBufferView(std::vector<std::unique_ptr<vk::raii::Buffer>> const& v)
{
    std::vector<vk::Buffer> buffer;
    std::transform(v.begin(), v.end(), std::back_inserter(buffer),
            [](std::unique_ptr<vk::raii::Buffer> const& b){return vk::Buffer(*b);});

    return buffer;
}
