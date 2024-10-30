#pragma once

#include "vk_helpers.h"
#include "vk_types.h"
#include "vk_mem_alloc.h"

#include "host_device_common.h"


class VulkanApp {
public:

	struct Buffer
	{
		VkBuffer		mBuffer;
		VmaAllocation	mAllocation;
		uint32_t		mByteSize;
	};

	struct AccelerationStructure
	{
		VkAccelerationStructureKHR	mHandle;
		Buffer						mData; // Stores the Acceleration structure data.
	};

	struct Scene
	{
		Camera				mCamera;

		std::vector<Sphere> mSpheres;
		Buffer				mSphereBuffer;
	};


	bool bUseValidationLayers{ true };
	VkExtent2D mWindowExtents{ 800, 600 };
	VkExtent2D mWorkGroupDim{ 16,16 };

	uint32_t mNumSamples{ 500 };
	uint32_t mNumBounces{ 32 };


	void initContext(bool validation);
	void initAllocators();
	void initImage();

	void uploadScene();

	void initAabbBlas();
	void initSceneTLAS();
	 
	void initDescriptorSets();
	void initComputePipeline();
	
	void render();
	void writeImage(const fs::path& path);
	void cleanup();



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

	// Image
	//-----------------------------------------------
	Buffer						mImageBuffer;
	
	// Acceleration structures
	//-----------------------------------------------
	AccelerationStructure		mAabbBlas;
	Buffer						mAabbGeometryBuffer;
	
	AccelerationStructure		mTlas;
	Buffer						mTlasInstanceBuffer;  // Stores the per-instance data (matrices, materialID etc...) 

	// Scene Data
	//-----------------------------------------------
	Scene						mScene;

	// Pipeline Data
	//-----------------------------------------------
	VkShaderModule				mComputeShader;
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