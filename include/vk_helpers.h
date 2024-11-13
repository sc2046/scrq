#pragma once

#include <vk_types.h>


inline VkDeviceAddress GetBufferDeviceAddress(VkDevice device, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo addressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer };
    return vkGetBufferDeviceAddress(device, &addressInfo);
}
inline VkDeviceAddress getBlasDeviceAddress(VkDevice device, VkAccelerationStructureKHR accel)
{
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = accel };
    return vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);
}


// TODO: Add additional usage, flags?
inline AllocatedBuffer createHostVisibleStagingBuffer(VmaAllocator allocator, uint32_t size_bytes)
{
    AllocatedBuffer buf;
    VkBufferCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size_bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo allocCreateInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };
    VK_CHECK(vmaCreateBuffer(allocator, &createInfo, &allocCreateInfo, &buf.mBuffer, &buf.mAllocation, &buf.mAllocInfo));
    return buf;
}

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

inline VkTransformMatrixKHR glmMat4ToVkTransformMatrixKHR(const glm::mat4& mat) {
    VkTransformMatrixKHR vkMatrix{};

    // Copy the first three rows and four columns from glm::mat4 to VkTransformMatrixKHR
    vkMatrix.matrix[0][0] = mat[0][0];
    vkMatrix.matrix[0][1] = mat[1][0];
    vkMatrix.matrix[0][2] = mat[2][0];
    vkMatrix.matrix[0][3] = mat[3][0];

    vkMatrix.matrix[1][0] = mat[0][1];
    vkMatrix.matrix[1][1] = mat[1][1];
    vkMatrix.matrix[1][2] = mat[2][1];
    vkMatrix.matrix[1][3] = mat[3][1];

    vkMatrix.matrix[2][0] = mat[0][2];
    vkMatrix.matrix[2][1] = mat[1][2];
    vkMatrix.matrix[2][2] = mat[2][2];
    vkMatrix.matrix[2][3] = mat[3][2];

    return vkMatrix;
}