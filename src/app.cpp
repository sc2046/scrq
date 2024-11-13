
#include <iostream>

#include <volk.h>

#include "app.h"

#include "vk_types.h"

#include <VkBootstrap.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// Submit operations to the queue, and wait for them to complete.
void VulkanApp::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(mDevice, 1, &mImmediateFence));
    VK_CHECK(vkResetCommandBuffer(mImmediateCmdBuf, 0));

    // Begin recording.
    VkCommandBufferBeginInfo cmdBeginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(mImmediateCmdBuf, &cmdBeginInfo));

    // Run commands.
    function(mImmediateCmdBuf);

    // End recording.
    VK_CHECK(vkEndCommandBuffer(mImmediateCmdBuf));

    VkCommandBufferSubmitInfo cmdinfo = {.sType =  VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    cmdinfo.commandBuffer = mImmediateCmdBuf;
    cmdinfo.deviceMask = 0;

    VkSubmitInfo2 submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdinfo;

    // Submit command buffer to the queue and immediately wait on the associated fence.
    VK_CHECK(vkQueueSubmit2(mComputeQueue, 1, &submit, mImmediateFence));
    VK_CHECK(vkWaitForFences(mDevice, 1, &mImmediateFence, true, 9999999999));
}

void VulkanApp::initContext(bool validation)
{
    // Create instance
    vkb::InstanceBuilder builder;

    const auto inst_ret = builder.set_app_name("Vulkan Compute Path Tracer")
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
    features13.synchronization2 = true;

    // Require these features for the extensions this app uses.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    asFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    asFeatures.accelerationStructure = true;
    
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    rayQueryFeatures.rayQuery = true;


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

    // Initialize VMA.
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

}

// Initialise all global vulkan resources needed for the program.
void VulkanApp::initResources()
{
    // Create a fence used for immediate submits.
    VkFenceCreateInfo fenceInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &mImmediateFence));
    mDeletionQueue.push_function([&](){vkDestroyFence(mDevice, mImmediateFence, nullptr);});

    // Create a global use command pool.
    // We will allocate one command buffer from this pool and re-use it.
    VkCommandPoolCreateInfo commandPoolCreateInfo   = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    commandPoolCreateInfo.flags                     = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;     
    commandPoolCreateInfo.queueFamilyIndex          = mComputeQueueFamily;
    VK_CHECK(vkCreateCommandPool(mDevice, &commandPoolCreateInfo, nullptr, &mCommandPool));
    mDeletionQueue.push_function([&]() { vkDestroyCommandPool(mDevice, mCommandPool, nullptr);});

    // Allocate a single command buffer that will be used for immediate submit commands.
    VkCommandBufferAllocateInfo cmdInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdInfo.commandBufferCount          = 1;
    cmdInfo.commandPool                 = mCommandPool;
    cmdInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdInfo, &mImmediateCmdBuf));

}

// Uploads all scene geometry into GPU buffers;
void VulkanApp::uploadScene()
{
    mScene = createAjaxScene();

    //TODO: Create one large staging buffer for all scene data? 
    
    // Upload triangle mesh data
    for(auto&& mesh : mScene.mMeshes)
    { 
        const size_t vertexBufferSize = mesh.mVertices.size() * sizeof(Vertex);
        const size_t indexBufferSize = mesh.mIndices.size() * sizeof(uint32_t);

        // Create GPU buffers for the vertices and indices.
        VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vertexBufferSize,
            .usage =
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mesh.mVertexBuffer.mBuffer, &mesh.mVertexBuffer.mAllocation, &mesh.mVertexBuffer.mAllocInfo));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mesh.mVertexBuffer.mBuffer, mesh.mVertexBuffer.mAllocation);});

        deviceBufferCreateInfo.size = indexBufferSize;
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mesh.mIndexBuffer.mBuffer, &mesh.mIndexBuffer.mAllocation, &mesh.mIndexBuffer.mAllocInfo));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mesh.mIndexBuffer.mBuffer, mesh.mIndexBuffer.mAllocation);});


        // Create a staging buffer for the mesh data.
        AllocatedBuffer meshStagingBuffer;
        const VkBufferCreateInfo stagingbufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = vertexBufferSize + indexBufferSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo stagingBufferAllocInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &meshStagingBuffer.mBuffer, &meshStagingBuffer.mAllocation, &meshStagingBuffer.mAllocInfo));

        // Copy mesh data to staging buffer.
        void* data;
        vmaMapMemory(mVmaAllocator, meshStagingBuffer.mAllocation, (void**)&data);
        memcpy(data, mesh.mVertices.data(), vertexBufferSize);
        memcpy((char*)data + vertexBufferSize, mesh.mIndices.data(), indexBufferSize);
        vmaUnmapMemory(mVmaAllocator, meshStagingBuffer.mAllocation); 

        // Transfer mesh data to GPU buffer.
        immediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy vertexCopy;
            vertexCopy.dstOffset = 0;
            vertexCopy.srcOffset = 0;
            vertexCopy.size      = vertexBufferSize;
            vkCmdCopyBuffer(cmd, meshStagingBuffer.mBuffer, mesh.mVertexBuffer.mBuffer, 1, &vertexCopy);

            VkBufferCopy indexCopy;
            indexCopy.dstOffset = 0;
            indexCopy.srcOffset = vertexBufferSize;
            indexCopy.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, meshStagingBuffer.mBuffer, mesh.mIndexBuffer.mBuffer, 1, &indexCopy);
        }); 

        // Staging buffer no longer needed.
        vmaDestroyBuffer(mVmaAllocator, meshStagingBuffer.mBuffer, meshStagingBuffer.mAllocation);
    }

    // Upload materials.
    {
        const auto materialsBufferSize = mScene.mMaterials.size() * sizeof(Material);

        // Create GPU buffer for the scene materials.
        const VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = materialsBufferSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mScene.mMaterialsBuffer.mBuffer, &mScene.mMaterialsBuffer.mAllocation, &mScene.mMaterialsBuffer.mAllocInfo));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mScene.mMaterialsBuffer.mBuffer, mScene.mMaterialsBuffer.mAllocation);});

        // Create a staging buffer.
        AllocatedBuffer materialsStagingBuffer;
        const VkBufferCreateInfo stagingbufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = materialsBufferSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo stagingBufferAllocInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &materialsStagingBuffer.mBuffer, &materialsStagingBuffer.mAllocation, &materialsStagingBuffer.mAllocInfo));

        // Copy material data to staging buffer.
        void* sdata;
        vmaMapMemory(mVmaAllocator, materialsStagingBuffer.mAllocation, (void**)&sdata);
        memcpy(sdata, mScene.mMaterials.data(), mScene.mMaterials.size() * sizeof(Material));
        vmaUnmapMemory(mVmaAllocator, materialsStagingBuffer.mAllocation);

        // Transfer material data from staging buffer to gpu buffer.
        immediateSubmit([&](VkCommandBuffer cmd) {
            const VkBufferCopy copy{ .srcOffset = 0, .dstOffset = 0, .size = materialsBufferSize };
            vkCmdCopyBuffer(cmd, materialsStagingBuffer.mBuffer, mScene.mMaterialsBuffer.mBuffer, 1, &copy);
            });

        // No longer need staging buffer.
        vmaDestroyBuffer(mVmaAllocator, materialsStagingBuffer.mBuffer, materialsStagingBuffer.mAllocation);
    }

    // Upload Textures 
    {
        stbi_set_flip_vertically_on_load(true);
        stbi_uc* pixels = stbi_load("assets/textures/statue.jpg", reinterpret_cast<int*>(&mTextureExtents.width), reinterpret_cast<int*>(&mTextureExtents.height), nullptr, STBI_rgb_alpha);
        mTextureByteSize = mTextureExtents.width * mTextureExtents.height * 4;
        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }

        // Copy pixel data to a staging buffer (apparently using a staging buffer is faster than a staging image)
        AllocatedBuffer imageStagingBuffer = createHostVisibleStagingBuffer(mVmaAllocator, mTextureByteSize);

        void* data;
        vmaMapMemory(mVmaAllocator, imageStagingBuffer.mAllocation, (void**)&data);
        memcpy(data, pixels, mTextureByteSize);
        vmaUnmapMemory(mVmaAllocator, imageStagingBuffer.mAllocation);

        stbi_image_free(pixels);

        // Create an image to store texture data.
        // Make sure to store the data in device-local memory.
        const VkImageCreateInfo imageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .extent = {mTextureExtents.width, mTextureExtents.height, 1} ,
            .mipLevels = 1,  
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,  // Use a gpu-friendly texel layout.
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        const VmaAllocationCreateInfo allocinfo = {
            .usage          = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        };
        VK_CHECK(vmaCreateImage(mVmaAllocator, &imageCreateInfo, &allocinfo, &mTextureImage.mImage, &mTextureImage.mAllocation, nullptr));
        mDeletionQueue.push_function([&]() {vmaDestroyImage(mVmaAllocator, mTextureImage.mImage, mTextureImage.mAllocation);});

        // Create an image view for the image.
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.image = mTextureImage.mImage;
        info.format = VK_FORMAT_R8G8B8A8_SRGB;
        info.subresourceRange = { 
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0,.layerCount = 1 };

        VK_CHECK(vkCreateImageView(mDevice, &info, nullptr, &mTextureImageView));
        mDeletionQueue.push_function([&]() {vkDestroyImageView(mDevice, mTextureImageView, nullptr);});


        // Transition the image layout into VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL for copying from staging buffer.
        // Then copy data from the staging buffer to the device.
        // Then transition the image layout into VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL for sampling from shader.
        immediateSubmit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier imageBarrier = {};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.image = mTextureImage.mImage;
            imageBarrier.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1 };

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageBarrier);

            // Copy data from staging buffer to image.
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { mTextureExtents.width, mTextureExtents.height, 1 };

            vkCmdCopyBufferToImage(cmd, imageStagingBuffer.mBuffer, mTextureImage.mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier imageBarrier2 = {};
            imageBarrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imageBarrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarrier2.image = mTextureImage.mImage;
            imageBarrier2.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1 };

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &imageBarrier2);
            });

        vmaDestroyBuffer(mVmaAllocator, imageStagingBuffer.mBuffer, imageStagingBuffer.mAllocation);
    }

    // Create sampler for the texture.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType           = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter       = VK_FILTER_NEAREST;
    samplerInfo.minFilter       = VK_FILTER_NEAREST;
    samplerInfo.addressModeU    = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV    = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    //samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    //TODO: Read about anisotropic filtering.
    //samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    //samplerInfo.unnormalizedCoordinates = VK_FALSE;
    //samplerInfo.compareEnable = VK_FALSE;
    //samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    //samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    //samplerInfo.mipLodBias = 0.0f;
    //samplerInfo.minLod = 0.0f;
    //samplerInfo.maxLod = 0.0f;
    VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mTextureSampler));
    mDeletionQueue.push_function([&] {vkDestroySampler(mDevice, mTextureSampler, nullptr);});
}

// Creates a blas for an AABB centered at the origin, with a half-extents of 1.
void VulkanApp::initAabbBlas()
{
    // First need to create a GPU buffer for the aabb, which will be used to build the BLAS. 
    {
        const AABB aabb{ .min = glm::vec3(-1.f), .max = glm::vec3(1.f)};
        const auto aabbBufferSize = sizeof(AABB);

        // Create GPU buffer for the AABB.
        const VkBufferCreateInfo deviceBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = aabbBufferSize,
            .usage =
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo deviceBufferAllocInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &deviceBufferCreateInfo, &deviceBufferAllocInfo, &mAabbGeometryBuffer.mBuffer, &mAabbGeometryBuffer.mAllocation, &mAabbGeometryBuffer.mAllocInfo));
        mDeletionQueue.push_function([&]() {vmaDestroyBuffer(mVmaAllocator, mAabbGeometryBuffer.mBuffer, mAabbGeometryBuffer.mAllocation);});

        // Create staging buffer for the AABB.
        AllocatedBuffer stagingBuffer;
        const VkBufferCreateInfo stagingbufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = aabbBufferSize,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo stagingBufferAllocInfo{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &stagingbufferCreateInfo, &stagingBufferAllocInfo, &stagingBuffer.mBuffer, &stagingBuffer.mAllocation, &stagingBuffer.mAllocInfo));

        // Copy data to the staging buffer.
        void* data;
        vmaMapMemory(mVmaAllocator, stagingBuffer.mAllocation, (void**)&data);
        memcpy(data, &aabb, sizeof(AABB));
        vmaUnmapMemory(mVmaAllocator, stagingBuffer.mAllocation);
        
        // Transfer data from stating buffer to GPU buffer.
        immediateSubmit([&](VkCommandBuffer cmd) {
            const VkBufferCopy copy{ .srcOffset = 0, .dstOffset = 0, .size = aabbBufferSize };
            vkCmdCopyBuffer(cmd, stagingBuffer.mBuffer, mAabbGeometryBuffer.mBuffer, 1, &copy);
            });

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
    AllocatedBuffer  scratchBuffer;
    {
        const VkBufferCreateInfo scratchBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.buildScratchSize ,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo scratchBufAllocCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, &scratchBuffer.mAllocInfo));
    }

    // Build the blas in a command buffer.
    immediateSubmit([&](VkCommandBuffer cmd) {
        // We can fill the rest of the buildGeometry struct.
        buildGeometryInfo.dstAccelerationStructure = mAabbBlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);
        const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ kAabbCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &buildRangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildGeometryInfo, &pRangeInfos);
        });

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
    AllocatedBuffer  scratchBuffer;
    {
        const VkBufferCreateInfo scratchBufCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buildSizesInfo.buildScratchSize ,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };
        const VmaAllocationCreateInfo scratchBufAllocCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
        VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, &scratchBuffer.mAllocInfo));
    }

    // Build the blas in a command buffer.
    immediateSubmit([&](VkCommandBuffer cmd) {
        // We can fill the rest of the buildGeometry struct.
        buildGeometryInfo.dstAccelerationStructure = mesh.mBlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);
        const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{ meshPrimitiveCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = &buildRangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildGeometryInfo, &pRangeInfos);
        });


    // We no longer need the scratch buffer.
    vmaDestroyBuffer(mVmaAllocator, scratchBuffer.mBuffer, scratchBuffer.mAllocation);
}

void VulkanApp::initSceneTLAS()
{
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    // TODO (Hack): For now, do triangle meshes first because the value of instanceCustomIndex will be used to index descriptors.
    // For triangle meshes, the custom index will store the id of the instance.
    for (uint32_t i = 0; i < mScene.mMeshes.size(); ++i)
    {
        instances.push_back(VkAccelerationStructureInstanceKHR{
            .transform = glmMat4ToVkTransformMatrixKHR(mScene.mMeshes[i].mTransform),
            .instanceCustomIndex = i,                                     
            .mask = 0xFF,                                                                       // No masking. Ray will always be visible.
            .instanceShaderBindingTableRecordOffset = mScene.mMeshes[i].mMaterialID,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,                 // No face culling, etc.
            .accelerationStructureReference = getBlasDeviceAddress(mDevice, mScene.mMeshes[i].mBlas.mHandle)  // For meshes, use the address of the mesh BLAS .
            });
    }

    // Create an instance for each sphere in the scene.
    // Each sphere instance has it's own transform, but shares the same custom index.
    for (uint32_t i = 0; i < mScene.mSpheres.size(); ++i)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.f), mScene.mSpheres[i].center);
        transform = glm::scale(transform, glm::vec3(mScene.mSpheres[i].radius));

        instances.push_back(VkAccelerationStructureInstanceKHR{
           .transform = glmMat4ToVkTransformMatrixKHR(transform),
           .instanceCustomIndex = SPHERE_CUSTOM_INDEX,                                 // We use the custom index in the shader to determine the geometry type.
           .mask = 0xFF,                                                                // No masking. Ray will always be visible.
           .instanceShaderBindingTableRecordOffset = mScene.mSpheres[i].materialID,       // We use this to determine the material of the surface.
           .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,          // No   face culling, etc.
           .accelerationStructureReference = getBlasDeviceAddress(mDevice, mAabbBlas.mHandle) // For procedural geometry, use the address of the AABB BLAS.
            });
    }

    // Other procedural Geometry...

    const uint32_t kInstanceCount =instances.size();

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
    AllocatedBuffer   scratchBuffer;
    const VkBufferCreateInfo scratchBufCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buildSizesInfo.buildScratchSize ,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo scratchBufAllocCreateInfo{ .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
    VK_CHECK(vmaCreateBuffer(mVmaAllocator, &scratchBufCreateInfo, &scratchBufAllocCreateInfo, &scratchBuffer.mBuffer, &scratchBuffer.mAllocation, &scratchBuffer.mAllocInfo));
    const auto scratchAddress = GetBufferDeviceAddress(mDevice, scratchBuffer.mBuffer);

    // Build the TLAS.
    immediateSubmit([&](VkCommandBuffer cmd) {
        // Update build info
        buildGeometryInfo.dstAccelerationStructure = mTlas.mHandle;
        buildGeometryInfo.scratchData.deviceAddress = scratchAddress;

        const VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{ kInstanceCount, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildGeometryInfo, &pBuildOffsetInfo);
        });
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
        .format = imageCreateInfo.format, // Use same format as defined in the image
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
    immediateSubmit([&](VkCommandBuffer cmd) {
        const VkAccessFlags dstImageRenderAccesses = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        const VkAccessFlags dstImageLinearAccesses = VK_ACCESS_TRANSFER_WRITE_BIT;

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
        imageBarriers[0].srcAccessMask = 0;
        imageBarriers[0].dstAccessMask = dstImageRenderAccesses;
        imageBarriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarriers[0].image = mImageRender.mImage;
        imageBarriers[0].subresourceRange = range;

        // Image memory barrier for `imageLinear` from UNDEFINED to TRANSFER_DST_OPTIMAL layout:
        // Later it will be the destination of a copy.
        imageBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarriers[1].srcAccessMask = 0;
        imageBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarriers[1].image = mImageLinear.mImage;
        imageBarriers[1].subresourceRange = range;


        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, //TODO: ??
            0,
            0, nullptr,
            0, nullptr,
            2, imageBarriers.data());
        });

}

// Defines the layout for the descriptor set.
// Creates a descriptor pool to allocate descriptors from.
// Allocates a descriptor set from the pool.
// Binds the descriptors in the set to their resources.
void VulkanApp::initDescriptorSets()
{
    std::vector<VkDescriptorSetLayoutBinding> bindingInfo;

    // For image data Buffer.
    bindingInfo.emplace_back(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    // For scene TLAS.
    bindingInfo.emplace_back(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    //TODO: (HACK) Make count == meshes.size()
    // Buffer for triangle mesh vertices.
    bindingInfo.emplace_back(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_MESH_COUNT, VK_SHADER_STAGE_COMPUTE_BIT);
    // Buffer for triangle mesh indices.
    bindingInfo.emplace_back(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_MESH_COUNT, VK_SHADER_STAGE_COMPUTE_BIT);
    // Buffer for scene materials.
    bindingInfo.emplace_back(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    // Sampler for scene textures.
    bindingInfo.emplace_back(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);


    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindingInfo.size()),
        .pBindings = bindingInfo.data()
    };
    VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &descriptorSetLayoutCreateInfo, nullptr, &mDescriptorSetLayout););
    mDeletionQueue.push_function([&]() {vkDestroyDescriptorSetLayout(mDevice, mDescriptorSetLayout, nullptr);});

    // Create a descriptor pool for the resources we will need.
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.emplace_back(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 );
    sizes.emplace_back(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 + (2 * MAX_MESH_COUNT) );
    sizes.emplace_back(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 );
    sizes.emplace_back(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);

    const VkDescriptorPoolCreateInfo info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = static_cast<uint32_t>(sizes.size()),
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

    // Bind the descriptor set to the resources.
    std::array<VkWriteDescriptorSet, 6> writeDescriptorSets;

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

    // Create a descriptor array for mesh vertices/indices.
    std::vector<VkDescriptorBufferInfo> meshVertexBufferDescriptorArrayInfo;
    std::vector<VkDescriptorBufferInfo> meshIndexBufferDescriptorArrayInfo;
    for (int i = 0;i < mScene.mMeshes.size(); ++i)
    {
        meshVertexBufferDescriptorArrayInfo.emplace_back(mScene.mMeshes[i].mVertexBuffer.mBuffer, 0, mScene.mMeshes[i].mVertices.size() * sizeof(Vertex));
        meshIndexBufferDescriptorArrayInfo.emplace_back(mScene.mMeshes[i].mIndexBuffer.mBuffer, 0, mScene.mMeshes[i].mIndices.size() * sizeof(uint32_t));
    }
    writeDescriptorSets[2] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(mScene.mMeshes.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = meshVertexBufferDescriptorArrayInfo.data()
    };
    writeDescriptorSets[3] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 3,
        .dstArrayElement = 0,
        .descriptorCount = static_cast<uint32_t>(mScene.mMeshes.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = meshIndexBufferDescriptorArrayInfo.data()
    };

    const VkDescriptorBufferInfo materialBufferDescriptorInfo{ .buffer = mScene.mMaterialsBuffer.mBuffer, .range = mScene.mMaterialsBuffer.mAllocInfo.size };
    writeDescriptorSets[4] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 4,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &materialBufferDescriptorInfo
    };

    const VkDescriptorImageInfo imageInfo{ .sampler = mTextureSampler, .imageView = mTextureImageView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writeDescriptorSets[5] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = mDescriptorSet,
        .dstBinding = 5,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };

    vkUpdateDescriptorSets(mDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void VulkanApp::initComputePipeline()
{

    // Specify specialization constants.
    std::array<VkSpecializationMapEntry, 1> specializationMapEntries;
    specializationMapEntries[0].constantID = 0;
    specializationMapEntries[0].size = sizeof(mSpecializationData.integrator);
    specializationMapEntries[0].offset = 0;

    VkSpecializationInfo specializationInfo;
    specializationInfo.dataSize         = sizeof(SpecializationData);
    specializationInfo.pData            = &mSpecializationData;
    specializationInfo.mapEntryCount    = static_cast<uint32_t>(specializationMapEntries.size());
    specializationInfo.pMapEntries      = specializationMapEntries.data();

    // Load shader module.
    mComputeShader = createShaderModule(mDevice, fs::path("shaders/book2/book2.spv"));
    const VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = mComputeShader,
        .pName = "main",
        .pSpecializationInfo = &specializationInfo
    };
    mDeletionQueue.push_function([&]() {vkDestroyShaderModule(mDevice, mComputeShader, nullptr); });

    // Specify specialization


    // Create a push constant range describing the amount of data for the push constants.
    static_assert(sizeof(Camera) % 4 == 0, "Push constant size must be a multiple of 4 per the Vulkan spec!");
    const VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Camera) + sizeof(SamplingParameters)};

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &mDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,

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
    for (uint32_t sampleBatch = 0; sampleBatch < mNumBatches; ++sampleBatch)
    {
        immediateSubmit([&](VkCommandBuffer cmd) {
            // Bind the compute pipeline and all the resources used by the pipeline.
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mComputePipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

            vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Camera), &mScene.mCamera);
            mSamplingParams.mBatchID = sampleBatch;
            vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(Camera), sizeof(SamplingParameters), &mSamplingParams);

            // Run the compute shader for this batch.
            vkCmdDispatch(cmd, std::ceil(mWindowExtents.width / 16), std::ceil(mWindowExtents.height / 16), 1);

            if (sampleBatch == mNumBatches - 1)
            {

                // Before we copy from the GPU image to the host-visible one, we need to 
                // 1) Perform an image layout transition for the GPU image.
                // 2) Place a memory barrier ensuring the compute shader has finished reading/writing from the GPU image before copying from it.
                {
                    VkImageMemoryBarrier imageBarrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;    // Shader reads/writes to this image must have completed and be available
                    imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;                               // Before reading from it in a transfer operation.
                    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    imageBarrier.image = mImageRender.mImage;
                    imageBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

                    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  // Src and dst pipeline stages
                        0,
                        0, nullptr, 0, nullptr,
                        1, &imageBarrier);

                    // Copy data from GPU image to Host-visible image.
                    VkImageCopy region{
                        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
                        .srcOffset = {0, 0, 0},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
                        .dstOffset = {0, 0, 0},
                        .extent = {mWindowExtents.width, mWindowExtents.height, 1}
                    };
                    vkCmdCopyImage(cmd,
                        mImageRender.mImage,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        mImageLinear.mImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &region);
                }


                // Place a global pipeline memory barrier that ensures all transfer writes must be made visible
                // Before reading from the Host.
                const VkMemoryBarrier memoryBarrier = {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_HOST_READ_BIT
                };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_HOST_BIT,
                    0,
                    1, &memoryBarrier,
                    0, nullptr, 0, nullptr);
            }
            fmt::print("\rRendered batch {}/{}", sampleBatch + 1, mNumBatches);
            });
    }
}

// Write image data to external file.
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
