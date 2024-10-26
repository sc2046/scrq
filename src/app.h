#pragma once

#include "vk_types.h"
#include "vk_mem_alloc.h"

#include "shader.h"

class VulkanApp {
public:

	bool bUseValidationLayers{ true };
	VkExtent2D mWindowExtents{ 1700 , 900 };
	VkExtent2D mWorkGroupDim{ 16,16 };

	void initContext(bool validation);
	void initAllocators();
	void initMesh();
	void initBLAS();
	void initTLAS();
	void initBuffers();
	void initDescriptorSets();
	void initComputePipeline();
	
	void run();
	void cleanup();


	//void initSpheres();
	//void initSphereBLAS();

	struct Mesh
	{
		std::vector<float> mVertices;
		std::vector<uint32_t> mIndices;
		inline static constexpr uint32_t kVertexStride = 3 * sizeof(float);
	};
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
	Buffer						mBlasBuffer;
	Buffer						mTlasBuffer;
	Buffer						mTlasInstanceBuffer;

	// Acceleration structures
	//-----------------------------------------------
	VkAccelerationStructureKHR	mBlas;
	VkAccelerationStructureKHR	mTlas;

	// Mesh Data
	//-----------------------------------------------
	Mesh						mMesh;
	Buffer						mVertexBuffer;
	Buffer						mIndexBuffer;

	// Sphere Data
	//-----------------------------------------------
	Buffer						mSphereBuffer;

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