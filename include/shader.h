#pragma once

#include "vk_types.h"

#include <fstream>

// Loads binary data from a file
inline std::vector<char> readBinaryFile(const fs::path& path)
{
    //Handle filesystem errors
    if (!fs::exists(path))
    {
        fmt::print("Error finding path to shader: {}", path.string());
    }

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}


inline VkShaderModule createShaderModule(VkDevice device, const fs::path& path)
{
    // Create shader module.
    const auto shaderByteCode = readBinaryFile(path);
    VkShaderModuleCreateInfo shaderModuleCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .flags = {},
    .codeSize = shaderByteCode.size(),
    .pCode = reinterpret_cast<const uint32_t*>(shaderByteCode.data())
    };
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule));

    return shaderModule;
}
