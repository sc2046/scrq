#include "app.h"

#include <chrono>

#ifdef NDEBUG
constexpr bool validation = false;
#else
constexpr bool validation = true;
#endif


int main()
{
    VulkanApp engine;
    
    // Initialization 
    {

        double vkInitTime;
        double sceneInitTime;
        double asInitTime;

        auto begin = std::chrono::high_resolution_clock::now();
        {
            auto begin = std::chrono::high_resolution_clock::now();
            engine.initContext(validation);
            engine.initAllocators();
            engine.initImage();
            auto end = std::chrono::high_resolution_clock::now();
            vkInitTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        }

        {
            auto begin = std::chrono::high_resolution_clock::now();
            engine.uploadScene();
            auto end = std::chrono::high_resolution_clock::now();
            sceneInitTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        }

        {
            auto begin = std::chrono::high_resolution_clock::now();
            engine.initAabbBlas();
            for (int i = 0; i < engine.mScene.mMeshes.size(); ++i)
            {
                engine.initMeshBlas(engine.mScene.mMeshes[i]);
            }
            engine.initSceneTLAS();
            auto end = std::chrono::high_resolution_clock::now();
            asInitTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        }

        engine.initDescriptorSets();
        engine.initComputePipeline();
        
        auto end = std::chrono::high_resolution_clock::now();
        fmt::println("Setup time: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
        fmt::println("\tVulkan context initialization: {}ms", vkInitTime);
        fmt::println("\tScene setup: {}ms", sceneInitTime);
        fmt::println("\tAcceleration structure setup: {}ms", asInitTime);
    }

    {
        auto begin = std::chrono::high_resolution_clock::now();
        engine.render();
        auto end = std::chrono::high_resolution_clock::now();
        fmt::println("Render time: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
        fmt::println("\tSample count: {}", engine.mNumSamples);
        fmt::println("\tRecursion depth: {}", engine.mNumBounces);
    }

    const fs::path sceneDirectory("../../scenes");
    const auto outPath = (sceneDirectory / engine.mScene.mName).replace_extension(".hdr");
    {
        auto begin = std::chrono::high_resolution_clock::now();
        engine.writeImage(outPath);
        auto end = std::chrono::high_resolution_clock::now();
        fmt::println("Image write time: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
        fmt::println("Image written to: {}", fs::absolute(outPath).string());
    }

    engine.cleanup();
}
