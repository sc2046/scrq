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

inline VkCommandBuffer AllocateAndBeginOneTimeCommandBuffer(VkDevice device, VkCommandPool cmdPool)
{
    VkCommandBufferAllocateInfo cmdAllocInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                             .commandPool = cmdPool,
                                             .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                             .commandBufferCount = 1 };
    VkCommandBuffer             cmdBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer));
    VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                       .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
    return cmdBuffer;
}
inline void EndSubmitWaitAndFreeCommandBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdPool, VkCommandBuffer& cmdBuffer)
{
    VK_CHECK(vkEndCommandBuffer(cmdBuffer));
    VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmdBuffer };
    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
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