﻿#pragma once

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>


#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>
#include <gtx/quaternion.hpp>
#include <gtc/matrix_transform.hpp>

namespace fs = std::filesystem;


#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::print("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)


struct AllocatedBuffer
{
    VkBuffer		    mBuffer;
    VmaAllocation	    mAllocation;
    VmaAllocationInfo	mAllocInfo;
};

struct Image
{
    VkImage         mImage;
    VmaAllocation   mAllocation;
};

struct AccelerationStructure
{
    VkAccelerationStructureKHR	        mHandle;
    AllocatedBuffer						mData; // Stores the Acceleration structure data.
};
