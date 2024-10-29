#pragma once

#include "vk_helpers.h"
#include "vk_types.h"
#include "vk_mem_alloc.h"

#include "shader.h"
#include "host_device_common.h"

class VulkanApp {
public:

	bool bUseValidationLayers{ true };
	VkExtent2D mWindowExtents{ 1700 , 900 };
	VkExtent2D mWorkGroupDim{ 16,16 };

	void initContext(bool validation);
	void initAllocators();
	void initImage();
	void initSpheres();
	void initSphereBLAS();
	void initSphereTLAS();
	void initDescriptorSets();
	void initComputePipeline();
	
	void run();
	void cleanup();

	struct Buffer
	{
		VkBuffer		mBuffer;
		VmaAllocation	mAllocation;
		uint32_t		mByteSize;
	};
private:
	
	// Vulkan context.
	//-----------------------------------------------
	VkInstance					mInstance;
	VkPhysicalDevice			mPhysicalDevice;
	VkDevice					mDevice;
	VkDebugUtilsMessengerEXT	mDebugMessenger;
	VkQueue						mComputeQueue;
	uint32_t					mComputeQueueFamily;

	// Allocators.
	//-----------------------------------------------
	VmaAllocator				mVmaAllocator;
	VkDescriptorPool			mDescriptorPool;
	VkCommandPool				mCommandPool;

	// Descriptors
	//-----------------------------------------------
	VkDescriptorSetLayout		mDescriptorSetLayout;
	VkDescriptorSet				mDescriptorSet;

	// Buffers
	//-----------------------------------------------
	Buffer						mImageBuffer;
	Buffer						mSphereBlasBuffer;
	Buffer						mSphereTlasBuffer;
	Buffer						mSphereTlasInstanceBuffer;

	// Acceleration structures
	//-----------------------------------------------
	VkAccelerationStructureKHR	mSphereBlas;
	VkAccelerationStructureKHR	mSphereTlas;

	// Scene Data
	//-----------------------------------------------
	std::vector<Sphere>			mSpheres;
	Buffer						mSphereBuffer;
	Buffer						mAABBSphereBuffer; // Need to store AABBs on GPU for the BLAS. (but dont need to store them on cpu, assuming they are static!)

	// Shaders
	//-----------------------------------------------
	VkShaderModule				mComputeShader;

	// Pipelines
	//-----------------------------------------------
	VkPipelineLayout			mPipelineLayout;
	VkPipeline					mComputePipeline;
	
	//-----------------------------------------------
	struct DeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)(); //call functors
			}

			deletors.clear();
		}
	};
	DeletionQueue mDeletionQueue;


};