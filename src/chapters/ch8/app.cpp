
#include <iostream>

#include <volk.h>

#include "app.h"

#include "vk_types.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

void VulkanApp::initContext(bool validation)
{
    // Create instance
    vkb::InstanceBuilder builder;

    const auto inst_ret = builder.set_app_name("Vulkan Compute Ray Tracer")
        .request_validation_layers(validation)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    if (!inst_ret) {
        fmt::print("Failed to create Vulkan instance: {}\n", inst_ret.error().message());
    }

    vkb::Instance vkb_instance = inst_ret.value();
    mInstance = vkb_instance.instance;
    mDebugMessenger = vkb_instance.debug_messenger;

    if (volkInitialize() != VK_SUCCESS) {
        // Handle initialization failure
    }
    volkLoadInstance(vkb_instance);


    // features from Vulkan 1.2.
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.scalarBlockLayout = true;

    // features from Vulkan 1.3.
    VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    // Require these features for the extensions this app uses.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    asFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .accelerationStructure = true };
    VkPhysicalDeviceRayQueryFeaturesKHR                 rayQueryFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, .rayQuery = true };


    // Select a physical device that supports the required extensions
    // Note that we don't need a surface for this project. 
    vkb::PhysicalDeviceSelector physDeviceSelector{ vkb_instance };
    const auto physDevice_ret = physDeviceSelector
        .set_minimum_version(1, 3)
        .defer_surface_initialization()
        .set_required_features_12(features12)
        .set_required_features_13(features13)

        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .add_required_extension_features(asFeatures)
        .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        .add_required_extension_features(rayQueryFeatures)
        .select();
    if (!physDevice_ret) {
        fmt::print("Failed to create Vulkan physical device: {}\n", physDevice_ret.error().message());
    }
    vkb::PhysicalDevice physicalDevice = physDevice_ret.value();
    mPhysicalDevice = physicalDevice.physical_device;

    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    const auto dev_ret = deviceBuilder.build();
    if (!dev_ret) {
        fmt::print("Failed to create Vulkan logical device: {}\n", dev_ret.error().message());
    }
    vkb::Device vkbDevice = dev_ret.value();

    // Get the VkDevice handle used in the rest of a vulkan application
    mDevice = vkbDevice.device;
    volkLoadDevice(mDevice);

    // Search device for a compute queue.
    const auto queue_ret = vkbDevice.get_queue(vkb::QueueType::compute);
    if (!queue_ret) {
        fmt::print("Failed to find a compute queue: {}\n", queue_ret.error().message());
    }
    mComputeQueue = queue_ret.value();
    mComputeQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::compute).value();
    VkQueue compute_queue = queue_ret.value();
    const auto compute_queue_index = vkbDevice.get_queue_index(vkb::QueueType::compute).value();

    // Add destroy functions to deletion queue.
    mDeletionQueue.push_function([&]() {    vkDestroyInstance(mInstance, nullptr);});
    mDeletionQueue.push_function([&]() {    vkb::destroy_debug_utils_messenger(mInstance, mDebugMessenger);});
    mDeletionQueue.push_function([&]() {    vkDestroyDevice(mDevice, nullptr);});
}

void VulkanApp::initAllocators()
{
    // Create memory allocator.
    VmaVulkanFunctions f{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr
    };
    VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = mPhysicalDevice,
        .device = mDevice,
        .pVulkanFunctions = &f,
        .instance = mInstance,
    };
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mVmaAllocator));
    mDeletionQueue.push_function([&]() {    vmaDestroyAllocator(mVmaAllocator);});

    // Create a single-use command pool.
    // Command buffers allocated from this pool will not be re-used.
    VkCommandPoolCreateInfo commanddPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    .queueFamilyIndex = mComputeQueueFamily,
    };
    VK_CHECK(vkCreateCommandPool(mDevice, &commanddPoolCreateInfo, nullptr, &mCommandPool));
    mDeletionQueue.push_function([&]() { vkDestroyCommandPool(mDevice, mCommandPool, nullptr);});
}



void VulkanApp::initSpheres()
{
    // Initialize spheres for the scene.

    mSpheres.emplace_back(glm::vec3(0.f, 0.f, -1.f), 0.5f);
    mSpheres.emplace_back(glm::vec3(0.f, -100.5f, -1.f), 100.f);

    // The AABBs of the spheres are needed for construction of the BLAS.
    std::vector<AABB> aabbs;
    aabbs.reserve(mSpheres.size());
    for (const auto& s : mSpheres)
    {
        AABB aabb{
            .min = s.center - glm::vec3(s.radius),
            .max = s.center + glm::vec3(s.radius)
        };
        aabbs.emplace_back(aabb);
    }
    VulkanApp::Buffer sphereStagingBuffer;
    sphereStagingBuffer.mByteSize = mSpheres.size() * sizeof(Sphere);

    VulkanApp::Buffer aabbStagingBuffer;
    aabbStagingBuffer.mByteSize = aabbs.size() * sizeof(AABB);

    VkBufferCreateInfo stagingbufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sphereStagingBuffer.mByteSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo stagingBufferAllocInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &sphereStagingBuffer.mBuffer, &sphereStagingBuffer.mAllocation, nullptr));

    void* sdata;
    vmaMapMemory(mVmaAllocator, sphereStagingBuffer.mAllocation, (void**)&sdata);
    memcpy(sdata, mSpheres.data(), mSpheres.size() * sizeof(Sphere));
    vmaUnmapMemory(mVmaAllocator, sphereStagingBuffer.mAllocation);

    // Create Staging buffer for aabbs.
    stagingbufferCreateInfo.size = aabbStagingBuffer.mByteSize;
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &aabbStagingBuffer.mBuffer, &aabbStagingBuffer.mAllocation, nullptr));

    void* adata;
    vmaMapMemory(mVmaAllocator, aabbStagingBuffer.mAllocation, (void**)&adata);
    memcpy(adata, aabbs.data(), aabbs.size() * sizeof(AABB));
    vmaUnmapMemory(mVmaAllocator, aabbStagingBuffer.mAllocation);

    // Create GPU buffers for the spheres and AABBs.
    mSphereBuffer.mByteSize     = sphereStagingBuffer.mByteSize;
    mAABBSphereBuffer.mByteSize = aabbStagingBuffer.mByteSize;

    VkBufferCreateInfo deviceBufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = mSphereBuffer.mByteSize,
        .usage =
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mSphereBuffer.mBuffer, &mSphereBuffer.mAllocation, nullptr));
    mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mSphereBuffer.mBuffer, mSphereBuffer.mAllocation);});

    deviceBufferCreateInfo.size = mAABBSphereBuffer.mByteSize;
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mAABBSphereBuffer.mBuffer, &mAABBSphereBuffer.mAllocation, nullptr));
    mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mAABBSphereBuffer.mBuffer, mAABBSphereBuffer.mAllocation);});


    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        VkBufferCopy copy;
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = mSphereBuffer.mByteSize;
        vkCmdCopyBuffer(cmdBuf, sphereStagingBuffer.mBuffer, mSphereBuffer.mBuffer, 1, &copy);

        copy.size = mAABBSphereBuffer.mByteSize;
        vkCmdCopyBuffer(cmdBuf, aabbStagingBuffer.mBuffer, mAABBSphereBuffer.mBuffer, 1, &copy);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    vmaDestroyBuffer(mVmaAllocator, sphereStagingBuffer.mBuffer, sphereStagingBuffer.mAllocation);
    vmaDestroyBuffer(mVmaAllocator, aabbStagingBuffer.mBuffer, aabbStagingBuffer.mAllocation);

}

void VulkanApp::initSphereBLAS()
{
    const uint32_t kSphereCount = mSpheres.size();

    const VkAccelerationStructureGeometryAabbsDataKHR aabbsData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
        .data = {.deviceAddress = GetBufferDeviceAddress(mDevice, mAABBSphereBuffer.mBuffer)}, 
        .stride = sizeof(AABB)
    };

    const VkAccelerationStructureGeometryKHR geometry{
        .sType          = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType   = VK_GEOMETRY_TYPE_AABBS_KHR,
        .geometry       = {.aabbs = aabbsData},
        .flags          = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    // This structure stores all the geometry and type data necessary for building the blas.
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, // We want a BLAS.
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    // The following structure stores the sizes required for the acceleration structure
    // We query the sizes, which will be filled in the BuildSizesInfo structure. 
    VkAccelerationStructureBuildSizesInfoKHR    buildSizesInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &kSphereCount, &buildSizesInfo);

    // Allocate a GPU scratch buffer holding the temporary data of the acceleration structure builder.
    VulkanApp::Buffer   scratchBuffer;
    {
        const VkBufferCreateInfo scratchBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.buildScratchSize ,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo scratchBufAllocCreateInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, nullptr));
    }
    const auto scratchAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);

    // Create a GPU buffer for the BLAS.
    {
        const VkBufferCreateInfo blasBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo blasBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &blasBufferCreateInfo, &blasBufferAllocInfo, &mSphereBlasBuffer.mBuffer, &mSphereBlasBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mSphereBlasBuffer.mBuffer, mSphereBlasBuffer.mAllocation);});
    }

    // Allocate memory for a blas.
    {
        const VkAccelerationStructureCreateInfoKHR blasCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mSphereBlasBuffer.mBuffer,
            .size = buildSizesInfo.accelerationStructureSize,                     // Size of the AS.
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR              //Type of the AS (BLAS in this case).
        };
        vkCreateAccelerationStructureKHR(mDevice, &blasCreateInfo, nullptr, &mSphereBlas);
        mDeletionQueue.push_function([&]() {vkDestroyAccelerationStructureKHR(mDevice, mSphereBlas, nullptr);});
    }

    // Build the blas in a command buffer.
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // We can fill the rest of the buildGeometry struct.
        buildGeometryInfo.dstAccelerationStructure = mSphereBlas;
        buildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);
        VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{
            .primitiveCount = kSphereCount, 
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0,
        };

        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &buildRangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildGeometryInfo, &pRangeInfos);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    // We no longer need the scratch buffer.
    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);
}

void VulkanApp::initSphereTLAS()
{
    // Create an instance referring to a blas. In this scene, we use a single instance with an identity transform.
    std::array< VkAccelerationStructureInstanceKHR, 1> instances;
    constexpr uint32_t instanceCount = instances.size();
    {
        VkTransformMatrixKHR transform = {};
        transform.matrix[0][0] = transform.matrix[1][1] = transform.matrix[2][2] = 1.f;
        instances[0] = {
           .transform = transform,
           .instanceCustomIndex = 0,  // Optional: custom index for this instance (available in shaders)
           .mask = 0xFF,  // Visibility mask, typically set to 0xFF to allow visibility in all ray types
           .instanceShaderBindingTableRecordOffset = 0,  // Offset in the shader binding table for this instance
           .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,  // No face culling, etc.
           .accelerationStructureReference = getBlasDeviceAddress(mDevice, mSphereBlas)  // The device address of the BLAS
        };
    }

    // Upload instances to the device via a staging buffer.
    {
        const VkBufferCreateInfo instanceBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(VkAccelerationStructureInstanceKHR) * instances.size(),
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        };
        VmaAllocationCreateInfo InstanceBufAllocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO, // Let the allocator decide which memory type to use.
        .requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | // Specify that the buffer may be mapped. 
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | // 
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT     // Without this flag, every read of the buffer's memory requires a fetch from GPU memory!
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &instanceBufCreateInfo, &InstanceBufAllocCreateInfo, &mSphereTlasInstanceBuffer.mBuffer, &mSphereTlasInstanceBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mSphereTlasInstanceBuffer.mBuffer, mSphereTlasInstanceBuffer.mAllocation);});

        void* data;
        vmaMapMemory(mVmaAllocator, mSphereTlasInstanceBuffer.mAllocation, (void**)&data);
        memcpy(data, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
        vmaUnmapMemory(mVmaAllocator, mSphereTlasInstanceBuffer.mAllocation);
    }

    //=======================================================
    //The rest is essentially the same as how we made the blas.
    //=======================================================

    // Specify shape and type of geometry for the AS.
    const VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,  // We are not using an array of pointers, just a flat buffer
        .data = {.deviceAddress = GetBufferDeviceAddress(mDevice, mSphereTlasInstanceBuffer.mBuffer)}
    };
    const VkAccelerationStructureGeometryKHR asGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {.instances = instancesData}
    };


    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .geometryCount = 1,
        .pGeometries = &asGeometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &instanceCount, &buildSizesInfo);

    // Create a GPU buffer for the TLAS.
    {
        const VkBufferCreateInfo tlasBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo tlasBufAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &tlasBufCreateInfo, &tlasBufAllocInfo, &mSphereTlasBuffer.mBuffer, &mSphereTlasBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mSphereTlasBuffer.mBuffer, mSphereTlasBuffer.mAllocation);});
    }
    // Allocate memory for a tlas
    {
        const VkAccelerationStructureCreateInfoKHR tlasCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mSphereTlasBuffer.mBuffer,
            .size = buildSizesInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
        };
        vkCreateAccelerationStructureKHR(mDevice, &tlasCreateInfo, nullptr, &mSphereTlas);
        mDeletionQueue.push_function([&]() {vkDestroyAccelerationStructureKHR(mDevice, mSphereTlas, nullptr);});
    }

    // Create scratch buffer for the tlas.
    VulkanApp::Buffer   scratchBuffer;
    {
        const VkBufferCreateInfo scratchBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.buildScratchSize ,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo scratchBufAllocCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, nullptr));
    }
    const auto scratchAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);

    // Build the TLAS.
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {

        // Update build info
        buildGeometryInfo.dstAccelerationStructure = mSphereTlas;
        buildGeometryInfo.scratchData.deviceAddress = scratchAddress;

        const VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{ instances.size(), 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildGeometryInfo, &pBuildOffsetInfo);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);
}

void VulkanApp::initImage()
{
    // Create a buffer containing the imagedata..
    // The ssbo will be accessed by the CPU and GPU, so we want it to be host-visible.
    mImageBuffer.mByteSize = mWindowExtents.height * mWindowExtents.width * 3 * sizeof(float);
    {
        VkBufferCreateInfo bufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = mImageBuffer.mByteSize,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        VmaAllocationCreateInfo allocInfo{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO, // Let the allocator decide which memory type to use.
            .requiredFlags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | // Specify that the buffer may be mapped. 
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | // 
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT     // Without this flag, every read of the buffer's memory requires a fetch from GPU memory!
        };
        vmaCreateBuffer(mVmaAllocator, &bufferCreateInfo, &allocInfo, &mImageBuffer.mBuffer, &mImageBuffer.mAllocation, nullptr);
    }
    mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mImageBuffer.mBuffer, mImageBuffer.mAllocation);});
}

void VulkanApp::initDescriptorSets()
{
    std::array<VkDescriptorSetLayoutBinding, 3> bindingInfo;
    // Buffer for image data.
    bindingInfo[0] = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, 
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    // TLAS for spheres.
    bindingInfo[1] = {
    .binding = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    // Buffer for sphere aabbs.
    bindingInfo[2] = {
    .binding = 2,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };

    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = bindingInfo.size(),
        .pBindings = bindingInfo.data()
    };
    VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &descriptorSetLayoutCreateInfo, nullptr, &mDescriptorSetLayout););
    mDeletionQueue.push_function([&]() {vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);});

    // Create a descriptor pool for the resources we will need.
    std::array<VkDescriptorPoolSize, 2> sizes;
    sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    sizes[1] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };

    const VkDescriptorPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 2,
    .pPoolSizes = sizes.data()
    };
    VK_CHECK(vkCreateDescriptorPool(mDevice, &info, nullptr, &mDescriptorPool););
    mDeletionQueue.push_function([&]() {    vkDestroyDescriptorPool(mDevice,mDescriptorPool,nullptr);});

    // Allocate a descriptor set from the pool.
    const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = mDescriptorPool,
        .descriptorSetCount = 1,            
        .pSetLayouts = &mDescriptorSetLayout
    };
    VK_CHECK(vkAllocateDescriptorSets(mDevice, &descriptorSetAllocInfo, &mDescriptorSet););

    // Point the descriptor sets to the resources.
    std::array<VkWriteDescriptorSet, 3> writeDescriptorSets;
    const VkDescriptorBufferInfo imageBufferDescriptor{.buffer = mImageBuffer.mBuffer, .range = mImageBuffer.mByteSize };
    writeDescriptorSets[0] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &imageBufferDescriptor
    };

    const VkWriteDescriptorSetAccelerationStructureKHR sphereTlasDescriptor{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
    .accelerationStructureCount = 1,
    .pAccelerationStructures = &mSphereTlas
    };
    writeDescriptorSets[1] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &sphereTlasDescriptor,  // This is important: the acceleration structure info goes in pNext
        .dstSet = mDescriptorSet,
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    };

    const VkDescriptorBufferInfo sphereBufferDescriptor{ .buffer = mSphereBuffer.mBuffer, .range = mSphereBuffer.mByteSize };
    writeDescriptorSets[2] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &sphereBufferDescriptor
    };

    vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void VulkanApp::initComputePipeline()
{
    // Load shader module.
    mComputeShader = createShaderModule(mDevice, fs::path("shaders/ch8/ch8.spv"));
    const VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = mComputeShader,
        .pName = "main",
    };
    mDeletionQueue.push_function([&]() {vkDestroyShaderModule(mDevice, mComputeShader, nullptr); });

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &mDescriptorSetLayout,
        .pushConstantRangeCount = 0
    };
    VK_CHECK(vkCreatePipelineLayout(mDevice, &pipelineLayoutCreateInfo, VK_NULL_HANDLE, &mPipelineLayout));
    mDeletionQueue.push_function([&]() {vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);  });

    VkComputePipelineCreateInfo computePipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = pipelineShaderStageCreateInfo,
        .layout = mPipelineLayout
    };
    VK_CHECK(vkCreateComputePipelines(mDevice, 
        VK_NULL_HANDLE,                        
        1, &computePipelineCreateInfo,         
        VK_NULL_HANDLE,                        
        &mComputePipeline)                     
    );
    mDeletionQueue.push_function([&]() {vkDestroyPipeline(mDevice, mComputePipeline, nullptr);});
}

void VulkanApp::run()
{
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // Bind the compute pipeline.
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, mComputePipeline);

        // Bind the descriptor set for the pipeline.
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

        // Run the compute shader.
        vkCmdDispatch(cmdBuf, std::ceil(mWindowExtents.width / 16), std::ceil(mWindowExtents.height / 16), 1);

        // Insert a pipeline barrier that ensures GPU memory writes are available for the CPU to read.
        VkMemoryBarrier memoryBarrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,  // Make shader writes
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT       // Readable by the CPU
        };
        vkCmdPipelineBarrier(cmdBuf,                                // The command buffer
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,           // From the transfer stage
            VK_PIPELINE_STAGE_HOST_BIT,               // To the CPU
            0,                                        // No special flags
            1, &memoryBarrier,                        // Pass the single global memory barrier.
            0, nullptr, 0, nullptr);                  // No image/buffer memory barriers.
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    // Write the buffer data to an external image.
    void* data;
    vmaMapMemory(mVmaAllocator, mImageBuffer.mAllocation, &data);
    stbi_write_hdr("../../scenes/ch8.hdr", mWindowExtents.width, mWindowExtents.height, 3, reinterpret_cast<float*>(data));
    vmaUnmapMemory(mVmaAllocator, mImageBuffer.mAllocation);
}


void VulkanApp::cleanup()
{
    // make sure the gpu has stopped doing its things				
    vkDeviceWaitIdle(mDevice);


    //flush the global deletion queue
    mDeletionQueue.flush();
}
