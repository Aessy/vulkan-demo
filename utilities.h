#pragma once

#include <vector>
#include <string>
#include <fstream>

inline std::vector<char> readFile(std::string const& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
