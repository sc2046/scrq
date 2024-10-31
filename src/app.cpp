
#include <iostream>

#include <volk.h>

#include "app.h"

#include "vk_types.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

// Uploads all scene geometry into GPU buffers;
void VulkanApp::uploadScene()
{
    mScene = createSponzaBuddhaScene();

    // Upload AABB (sphere) data.
    {
        Buffer sphereStagingBuffer;
        sphereStagingBuffer.mByteSize = mScene.mSpheres.size() * sizeof(Sphere);

        const VkBufferCreateInfo stagingbufferCreateInfo{
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
        memcpy(sdata, mScene.mSpheres.data(), mScene.mSpheres.size() * sizeof(Sphere));
        vmaUnmapMemory(mVmaAllocator, sphereStagingBuffer.mAllocation);

        // Create GPU buffer for the spheres.
        mScene.mSphereBuffer.mByteSize = sphereStagingBuffer.mByteSize;

        const VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = mScene.mSphereBuffer.mByteSize,
            .usage =
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mScene.mSphereBuffer.mBuffer, &mScene.mSphereBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mScene.mSphereBuffer.mBuffer, mScene.mSphereBuffer.mAllocation);});

        VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
        {
            VkBufferCopy copy{ .srcOffset = 0, .dstOffset = 0, .size = mScene.mSphereBuffer.mByteSize };
            vkCmdCopyBuffer(cmdBuf, sphereStagingBuffer.mBuffer, mScene.mSphereBuffer.mBuffer, 1, &copy);
        }
        EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);
        vmaDestroyBuffer(mVmaAllocator, sphereStagingBuffer.mBuffer, sphereStagingBuffer.mAllocation);
    }

    //TODO: Use same staging buffer for all meshes (set size equal to largest of meshes).
    // Upload triangle mesh data
    for(auto& mesh : mScene.mMeshes)
    { 
        // Create CPU staging buffers for the mesh data.
        Buffer vertexStagingBuffer;
        vertexStagingBuffer.mByteSize = mesh.mVertices.size() * sizeof(Vertex);
        Buffer indexStagingBuffer;
        indexStagingBuffer.mByteSize = mesh.mIndices.size() * sizeof(uint32_t);

        VkBufferCreateInfo stagingbufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = vertexStagingBuffer.mByteSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo stagingBufferAllocInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &vertexStagingBuffer.mBuffer, &vertexStagingBuffer.mAllocation, nullptr));

        stagingbufferCreateInfo.size = indexStagingBuffer.mByteSize;
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &indexStagingBuffer.mBuffer, &indexStagingBuffer.mAllocation, nullptr));


        void* vdata;
        vmaMapMemory(mVmaAllocator, vertexStagingBuffer.mAllocation, (void**)&vdata);
        memcpy(vdata, mesh.mVertices.data(), mesh.mVertices.size() * sizeof(Vertex));
        vmaUnmapMemory(mVmaAllocator, vertexStagingBuffer.mAllocation);

        void* idata;
        vmaMapMemory(mVmaAllocator, indexStagingBuffer.mAllocation, (void**)&idata);
        memcpy(idata, mesh.mIndices.data(), mesh.mIndices.size() * sizeof(uint32_t));
        vmaUnmapMemory(mVmaAllocator, indexStagingBuffer.mAllocation);

        // Create GPU buffers for the vertices and indices.
        mesh.mVertexBuffer.mByteSize = vertexStagingBuffer.mByteSize;
        mesh.mIndexBuffer.mByteSize = indexStagingBuffer.mByteSize;
        VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = mesh.mVertexBuffer.mByteSize,
            .usage =
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mesh.mVertexBuffer.mBuffer, &mesh.mVertexBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mesh.mVertexBuffer.mBuffer, mesh.mVertexBuffer.mAllocation);});

        deviceBufferCreateInfo.size = mesh.mIndexBuffer.mByteSize;
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mesh.mIndexBuffer.mBuffer, &mesh.mIndexBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mesh.mIndexBuffer.mBuffer, mesh.mIndexBuffer.mAllocation);});


        VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
        {
            VkBufferCopy copy;
            copy.dstOffset = 0;
            copy.srcOffset = 0;
            copy.size = mesh.mVertexBuffer.mByteSize;
            vkCmdCopyBuffer(cmdBuf, vertexStagingBuffer.mBuffer, mesh.mVertexBuffer.mBuffer, 1, &copy);
            copy.size = mesh.mIndexBuffer.mByteSize;
            vkCmdCopyBuffer(cmdBuf, indexStagingBuffer.mBuffer, mesh.mIndexBuffer.mBuffer, 1, &copy);
        }
        EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

        // Staging buffers are no longer needed.
        vmaDestroyBuffer(mVmaAllocator, vertexStagingBuffer.mBuffer, vertexStagingBuffer.mAllocation);
        vmaDestroyBuffer(mVmaAllocator, indexStagingBuffer.mBuffer, indexStagingBuffer.mAllocation);

    }
}

// Creates a blas for an AABB. In local space, the AABB is centered at the origin with a half-extents of 1.
void VulkanApp::initAabbBlas()
{
    // First need to create buffer for the aabb, which will be used to build the BLAS. 
    {
        const AABB aabb{
            .min = glm::vec3(-1.f),
            .max = glm::vec3(1.f),
        };

        Buffer stagingBuffer;
        stagingBuffer.mByteSize = sizeof(AABB);

        VkBufferCreateInfo stagingbufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = stagingBuffer.mByteSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo stagingBufferAllocInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &stagingBuffer.mBuffer, &stagingBuffer.mAllocation, nullptr));

        void* sdata;
        vmaMapMemory(mVmaAllocator, stagingBuffer.mAllocation, (void**)&sdata);
        memcpy(sdata, &aabb, sizeof(AABB));
        vmaUnmapMemory(mVmaAllocator, stagingBuffer.mAllocation);

        // Create GPU buffer for the AABB.
        mAabbGeometryBuffer.mByteSize = stagingBuffer.mByteSize;
        VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = mAabbGeometryBuffer.mByteSize,
            .usage =
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mAabbGeometryBuffer.mBuffer, &mAabbGeometryBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mAabbGeometryBuffer.mBuffer, mAabbGeometryBuffer.mAllocation);});


        VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
        {
            const VkBufferCopy copy{ .srcOffset = 0, .dstOffset = 0, .size = mAabbGeometryBuffer.mByteSize };
            vkCmdCopyBuffer(cmdBuf, stagingBuffer.mBuffer, mAabbGeometryBuffer.mBuffer, 1, &copy);
        }
        EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);
        vmaDestroyBuffer(mVmaAllocator, stagingBuffer.mBuffer, stagingBuffer.mAllocation);
    }

    const uint32_t kAabbCount = 1;
    const VkAccelerationStructureGeometryAabbsDataKHR aabbData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
        .data = {.deviceAddress = GetBufferDeviceAddress(mDevice, mAabbGeometryBuffer.mBuffer)},
        .stride = sizeof(AABB)
    };

    const VkAccelerationStructureGeometryKHR geometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
        .geometry = {.aabbs = aabbData},
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
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
    vkGetAccelerationStructureBuildSizesKHR(mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &kAabbCount, &buildSizesInfo);

    // Create blas handle and GPU buffer that will store the data.
    {
        const VkBufferCreateInfo blasBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo blasBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &blasBufferCreateInfo, &blasBufferAllocInfo, &mAabbBlas.mData.mBuffer, &mAabbBlas.mData.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mAabbBlas.mData.mBuffer, mAabbBlas.mData.mAllocation);});

        const VkAccelerationStructureCreateInfoKHR blasCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mAabbBlas.mData.mBuffer,
            .size = buildSizesInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
        };
        vkCreateAccelerationStructureKHR(mDevice, &blasCreateInfo, nullptr, &mAabbBlas.mHandle);
        mDeletionQueue.push_function([&]() {vkDestroyAccelerationStructureKHR(mDevice, mAabbBlas.mHandle, nullptr);});
    }


    // Allocate a GPU scratch buffer holding the temporary data of the acceleration structure builder.
    Buffer  scratchBuffer;
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

    // Build the blas in a command buffer.
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // We can fill the rest of the buildGeometry struct.
        buildGeometryInfo.dstAccelerationStructure = mAabbBlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);
        const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ kAabbCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &buildRangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildGeometryInfo, &pRangeInfos);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    // We no longer need the scratch buffer.
    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);
}

// Creates a blas for a triangle mesh. The geometry is defined in model space.
void VulkanApp::initMeshBlas(ObjMesh& mesh)
{
    const uint32_t        meshPrimitiveCount = static_cast<uint32_t>(mesh.mIndices.size() / 3); // number of triangles in the mesh.

    const VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = {.deviceAddress = GetBufferDeviceAddress(mDevice, mesh.mVertexBuffer.mBuffer)},
        .vertexStride = sizeof(Vertex),
        .maxVertex = static_cast<uint32_t>(mesh.mVertices.size() - 1),
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData = {.deviceAddress = GetBufferDeviceAddress(mDevice, mesh.mIndexBuffer.mBuffer)},
        .transformData = {.deviceAddress = 0} //TODO: Dont understand the use of this?
    };

    const VkAccelerationStructureGeometryKHR geometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry = {.triangles = trianglesData},
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    // This structure stores all the geometry and type data necessary for building the blas.
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, 
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    // The following structure stores the sizes required for the acceleration structure
    // We query the sizes, which will be filled in the BuildSizesInfo structure. 
    VkAccelerationStructureBuildSizesInfoKHR    buildSizesInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &meshPrimitiveCount, &buildSizesInfo);

    // Create blas handle and GPU buffer that will store the data.
    {
        const VkBufferCreateInfo blasBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo blasBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &blasBufferCreateInfo, &blasBufferAllocInfo, &mesh.mBlas.mData.mBuffer, &mesh.mBlas.mData.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mesh.mBlas.mData.mBuffer, mesh.mBlas.mData.mAllocation);});

        const VkAccelerationStructureCreateInfoKHR blasCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mesh.mBlas.mData.mBuffer,
            .size = buildSizesInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
        };
        vkCreateAccelerationStructureKHR(mDevice, &blasCreateInfo, nullptr, &mesh.mBlas.mHandle);
        mDeletionQueue.push_function([&]() {vkDestroyAccelerationStructureKHR(mDevice, mesh.mBlas.mHandle, nullptr);});
    }


    // Allocate a GPU scratch buffer holding the temporary data of the acceleration structure builder.
    Buffer  scratchBuffer;
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

    // Build the blas in a command buffer.
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // We can fill the rest of the buildGeometry struct.
        buildGeometryInfo.dstAccelerationStructure = mesh.mBlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);
        const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ meshPrimitiveCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &buildRangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildGeometryInfo, &pRangeInfos);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    // We no longer need the scratch buffer.
    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);
}

void VulkanApp::initSceneTLAS()
{
    uint32_t instanceIndex = 0;
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    // TODO (Hack): For now, do triangle meshes first because the value of instanceCustomIndex will be used to index descriptors.
    // Create an instance for each triangle mesh in the scene.
    for (uint32_t i = 0; i < mScene.mMeshes.size(); ++i)
    {
        instances.push_back(VkAccelerationStructureInstanceKHR{
            .transform = glmMat4ToVkTransformMatrixKHR(mScene.mMeshes[i].mTransform),                         
            .instanceCustomIndex = instanceIndex++,                                             // We use the custom index in the shader to access the instance transform.
            .mask = 0xFF,                                                                       // No masking. Ray will always be visible.
            .instanceShaderBindingTableRecordOffset = 0,                                        // TODO: Determine
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,                 // No face culling, etc.
            .accelerationStructureReference = getBlasDeviceAddress(mDevice, mScene.mMeshes[i].mBlas.mHandle)  // For meshes, use the address of the mesh BLAS .
            });
    }

    // Create an instance for each sphere in the scene.
    for (uint32_t i = 0; i < mScene.mSpheres.size(); ++i)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.f), mScene.mSpheres[i].center);
        transform = glm::scale(transform, glm::vec3(mScene.mSpheres[i].radius));

        instances.push_back(VkAccelerationStructureInstanceKHR{
           .transform = glmMat4ToVkTransformMatrixKHR(transform),
           .instanceCustomIndex = instanceIndex++,                                                    // We use the custom index in the shader to access the instance transform.
           .mask = 0xFF,                                                                // No masking. Ray will always be visible.
           .instanceShaderBindingTableRecordOffset = mScene.mSpheres[i].material,       // We use this to determine the material of the surface.
           .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,          // No   face culling, etc.
           .accelerationStructureReference = getBlasDeviceAddress(mDevice, mAabbBlas.mHandle) // For procedural geometry, use the address of the AABB BLAS.
        });
    }
    const uint32_t kInstanceCount = instances.size();

    // Create instances for quads...
    
    //TODO: Finish
    // Upload instanceData to the device via a staging buffer.
    {
        const VkBufferCreateInfo instanceBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(VkAccelerationStructureInstanceKHR) * instances.size(),
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        };
        const VmaAllocationCreateInfo instanceBufAllocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO, // Let the allocator decide which memory type to use.
        .requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | // Specify that the buffer may be mapped. 
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | // 
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT     // Without this flag, every read of the buffer's memory requires a fetch from GPU memory!
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &instanceBufCreateInfo, &instanceBufAllocCreateInfo, &mTlasInstanceBuffer.mBuffer, &mTlasInstanceBuffer.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mTlasInstanceBuffer.mBuffer, mTlasInstanceBuffer.mAllocation);});

        void* data;
        vmaMapMemory(mVmaAllocator, mTlasInstanceBuffer.mAllocation, (void**)&data);
        memcpy(data, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
        vmaUnmapMemory(mVmaAllocator, mTlasInstanceBuffer.mAllocation);
    }

    // Now we can build the TLAS.

    // Specify shape and type of geometry for the AS.
    const VkAccelerationStructureGeometryInstancesDataKHR instancesData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,  // We are not using an array of pointers, just a flat buffer
        .data = {.deviceAddress = GetBufferDeviceAddress(mDevice, mTlasInstanceBuffer.mBuffer)}
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
    vkGetAccelerationStructureBuildSizesKHR(mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &kInstanceCount, &buildSizesInfo);

    // Create tlas handle and GPU buffer that will store the data.
    {
        const VkBufferCreateInfo tlasBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.accelerationStructureSize,
            .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo tlasBufAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &tlasBufCreateInfo, &tlasBufAllocInfo, &mTlas.mData.mBuffer, &mTlas.mData.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mTlas.mData.mBuffer, mTlas.mData.mAllocation);});

        const VkAccelerationStructureCreateInfoKHR tlasCreateInfo{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = mTlas.mData.mBuffer,
            .size = buildSizesInfo.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
        };
        vkCreateAccelerationStructureKHR(mDevice, &tlasCreateInfo, nullptr, &mTlas.mHandle);
        mDeletionQueue.push_function([&]() {vkDestroyAccelerationStructureKHR(mDevice, mTlas.mHandle, nullptr);});
    }

    // Create scratch buffer for the tlas.
    Buffer   scratchBuffer;
    const VkBufferCreateInfo scratchBufCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buildSizesInfo.buildScratchSize ,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo scratchBufAllocCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, nullptr));
    const auto scratchAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);

    // Build the TLAS.
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // Update build info
        buildGeometryInfo.dstAccelerationStructure = mTlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = scratchAddress;

        const VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{ kInstanceCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildGeometryInfo, &pBuildOffsetInfo);
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);

    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);

}

void VulkanApp::initImage()
{
    // Create an image that will be used to write to in the compute shader.
    VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent = {.width = mWindowExtents.width, .height = mWindowExtents.height, .depth = 1 },
        .mipLevels = 1,  // No mip-maps
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, // Use a gpu-friendly layout.
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // We will be copying to a host-visible image.
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo imageGPUAllocCreateInfo{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VK_CHECK(vmaCreateImage(mVmaAllocator, &imageCreateInfo, &imageGPUAllocCreateInfo, &mImageRender.mImage, &mImageRender.mAllocation, nullptr));
    mDeletionQueue.push_function([&]() {vmaDestroyImage(mVmaAllocator, mImageRender.mImage, mImageRender.mAllocation);});


    // Create an image view for the render image.
    VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = mImageRender.mImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT, // Use same format as defined in the image
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0,.layerCount = 1 }
    };
    VK_CHECK(vkCreateImageView(mDevice, &imageViewCreateInfo, nullptr, &mImageView));
    mDeletionQueue.push_function([&]() {vkDestroyImageView(mDevice, mImageView, nullptr);});


    // Create another image for host-device transfer.
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VmaAllocationCreateInfo imageLinearAllocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
    };
    VK_CHECK(vmaCreateImage(mVmaAllocator, &imageCreateInfo, &imageLinearAllocCreateInfo, &mImageLinear.mImage, &mImageLinear.mAllocation, nullptr));
    mDeletionQueue.push_function([&]() {vmaDestroyImage(mVmaAllocator, mImageLinear.mImage, mImageLinear.mAllocation);});



    // Perform layout transitions using image barriers.
    //
    // 1) We'll need to make imageRender accessible to shader reads and writes, 
    // and transition it from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_GENERAL.
    //
    // 2) We'll need to make imageLinear accessible to transfer writes (since we'll copy image to it later), 
    // and transition it from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
    VkCommandBuffer uploadCmdBuffer = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {

        const VkAccessFlags srcAccesses             = 0; // (since image and imageLinear aren't initially accessible)
        const VkAccessFlags dstImageRenderAccesses  = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;   
        const VkAccessFlags dstImageLinearAccesses  = VK_ACCESS_TRANSFER_WRITE_BIT;                    

        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        // Image memory barrier for `imageRender` from UNDEFINED to GENERAL layout:
        std::array<VkImageMemoryBarrier, 2> imageBarriers = {};
        imageBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarriers[0].srcAccessMask = srcAccesses;
        imageBarriers[0].dstAccessMask = dstImageRenderAccesses;
        imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarriers[0].image = mImageRender.mImage;
        imageBarriers[0].subresourceRange = range;

        // Image memory barrier for `imageLinear` from UNDEFINED to TRANSFER_DST_OPTIMAL layout:
        imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarriers[1].srcAccessMask = srcAccesses;
        imageBarriers[1].dstAccessMask = dstImageLinearAccesses;
        imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarriers[1].image = mImageLinear.mImage;
        imageBarriers[1].subresourceRange = range;

        // Here's how to do that:
        const VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; //nvvk::makeAccessMaskPipelineStageFlags(srcAccesses);
        const VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;//nvvk::makeAccessMaskPipelineStageFlags(dstImageAccesses | dstImageLinearAccesses);

        // Include the two image barriers in the pipeline barrier:
        vkCmdPipelineBarrier(uploadCmdBuffer,       // The command buffer
            srcStages, dstStages,  // Src and dst pipeline stages
            0,                     // Flags for memory dependencies
            0, nullptr,            // Global memory barrier objects
            0, nullptr,            // Buffer memory barrier objects
            2, imageBarriers.data());     // Image barrier objects
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, uploadCmdBuffer);

}

void VulkanApp::initDescriptorSets()
{
    std::array<VkDescriptorSetLayoutBinding, 5> bindingInfo;
    // Buffer for image data.
    bindingInfo[0] = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1, 
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    // Scene TLAS.
    bindingInfo[1] = {
    .binding = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    // Buffer for AABBs.
    bindingInfo[2] = {
    .binding = 2,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };

    //TODO: (HACK) Make count == meshes.size()
    
    // Buffer for triangle mesh vertices.
    bindingInfo[3] = {
    .binding = 3,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = MAX_MESH_COUNT,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    // Buffer for triangle mesh indices.
    bindingInfo[4] = {
        .binding = 4,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = MAX_MESH_COUNT,
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
    std::array<VkDescriptorPoolSize, 3> sizes;
    sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 };
    sizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 + (2 * MAX_MESH_COUNT) };
    sizes[2] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };

    const VkDescriptorPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 3,
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
    std::array<VkWriteDescriptorSet, 5> writeDescriptorSets;

    const VkDescriptorImageInfo imageLinearDescriptor{ .imageView = mImageView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    writeDescriptorSets[0] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &imageLinearDescriptor
    };

    const VkWriteDescriptorSetAccelerationStructureKHR tlasDescriptor{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
    .accelerationStructureCount = 1,
    .pAccelerationStructures = &mTlas.mHandle
    };
    writeDescriptorSets[1] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &tlasDescriptor,  // This is important: the acceleration structure info goes in pNext
        .dstSet = mDescriptorSet,
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    };

    const VkDescriptorBufferInfo sphereBufferDescriptor{ .buffer = mScene.mSphereBuffer.mBuffer, .range = mScene.mSphereBuffer.mByteSize };
    writeDescriptorSets[2] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &sphereBufferDescriptor
    };

    // Create a descriptor array for mesh vertices/indices.
    std::vector<VkDescriptorBufferInfo> meshVertexBufferDescriptorArrayInfo;
    std::vector<VkDescriptorBufferInfo> meshIndexBufferDescriptorArrayInfo;
    for (int i = 0;i < mScene.mMeshes.size(); ++i)
    {
        meshVertexBufferDescriptorArrayInfo.emplace_back(mScene.mMeshes[i].mVertexBuffer.mBuffer, 0, mScene.mMeshes[i].mVertexBuffer.mByteSize);
        meshIndexBufferDescriptorArrayInfo.emplace_back(mScene.mMeshes[i].mIndexBuffer.mBuffer, 0, mScene.mMeshes[i].mIndexBuffer.mByteSize);
    }
    writeDescriptorSets[3] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 3,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(mScene.mMeshes.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = meshVertexBufferDescriptorArrayInfo.data()
    };
    writeDescriptorSets[4] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 4,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(mScene.mMeshes.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = meshIndexBufferDescriptorArrayInfo.data()
    };

    vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void VulkanApp::initComputePipeline()
{
    // Load shader module.
    mComputeShader = createShaderModule(mDevice, fs::path("shaders/book2/book2.spv"));
    const VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = mComputeShader,
        .pName = "main",
    };
    mDeletionQueue.push_function([&]() {vkDestroyShaderModule(mDevice, mComputeShader, nullptr); });

    // Create a push constant range describing the amount of data for the push constants.
    static_assert(sizeof(Camera) % 4 == 0, "Push constant size must be a multiple of 4 per the Vulkan spec!");
    const VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Camera) + 2*sizeof(uint32_t)};

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &mDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
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

void VulkanApp::render()
{
    VkCommandBuffer cmdBuf = AllocateAndBeginOneTimeCommandBuffer(mDevice, mCommandPool);
    {
        // Bind the compute pipeline.
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, mComputePipeline);

        // Bind the descriptor set for the pipeline.
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

        vkCmdPushConstants(cmdBuf, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Camera), &mScene.mCamera);                          
        vkCmdPushConstants(cmdBuf, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(Camera), sizeof(uint), &mNumSamples);
        vkCmdPushConstants(cmdBuf, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(Camera) + sizeof(uint), sizeof(uint), &mNumBounces);

        // Run the compute shader.
        vkCmdDispatch(cmdBuf, std::ceil(mWindowExtents.width / 16), std::ceil(mWindowExtents.height / 16), 1);


        // Transition `imageRender` from GENERAL to TRANSFER_SRC_OPTIMAL layout.
        
        const VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        // Image memory barrier for `imageRender` from UNDEFINED to GENERAL layout:
        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageBarrier.image = mImageRender.mImage;
        imageBarrier.subresourceRange = range;

        vkCmdPipelineBarrier(cmdBuf,             // Command buffer
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,  // Src and dst pipeline stages
            0,                     // Dependency flags
            0, nullptr,            // Global memory barriers
            0, nullptr,            // Buffer memory barriers
            1, &imageBarrier);          // Image memory barriers

        // We copy image color, mip 0, layer 0:
        {
            // We copy image color, mip 0, layer 0:
            VkImageCopy region{ .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,  
                                                  .mipLevel = 0,                          
                                                  .baseArrayLayer = 0,                          
                                                  .layerCount = 1
            },
                .srcOffset = {0, 0, 0}, // (0, 0, 0) in the first image corresponds to (0, 0, 0) in the second image:
                .dstSubresource = region.srcSubresource,
                .dstOffset = {0, 0, 0},
                .extent = {mWindowExtents.width, mWindowExtents.height, 1} };                 // Copy the entire image:
            vkCmdCopyImage(cmdBuf,                             // Command buffe
                mImageRender.mImage,                           // Source image
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // Source image layout
                mImageLinear.mImage,                     // Destination image
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  // Destination image layout
                1, &region);                           // Regions
        }

        // Insert a pipeline barrier that ensures GPU memory writes are available for the CPU to read.
        VkMemoryBarrier memoryBarrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,  // Make shader writes
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT       // Readable by the CPU
        };
        vkCmdPipelineBarrier(cmdBuf,                    // The command buffer
            VK_PIPELINE_STAGE_TRANSFER_BIT,       // From the transfer stage
            VK_PIPELINE_STAGE_HOST_BIT,               // To the CPU
            0,                                        // No special flags
            1, &memoryBarrier,                        // Pass the single global memory barrier.
            0, nullptr, 0, nullptr);                  // No image/buffer memory barriers.
    }
    EndSubmitWaitAndFreeCommandBuffer(mDevice, mComputeQueue, mCommandPool, cmdBuf);
}

void VulkanApp::writeImage(const fs::path& path)
{
    void* data;
    vmaMapMemory(mVmaAllocator, mImageLinear.mAllocation, &data);
    stbi_write_hdr(path.string().data(), mWindowExtents.width, mWindowExtents.height, 4, reinterpret_cast<float*>(data));
    vmaUnmapMemory(mVmaAllocator, mImageLinear.mAllocation);
}


void VulkanApp::cleanup()
{
    vkDeviceWaitIdle(mDevice);
    mDeletionQueue.flush();
}
