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
    engine.initVulkanContext(validation);
    engine.initVulkanResources();
    engine.initImages();

    // Upload scene data to the GPU.
    engine.uploadScene();

    // Initialize the acceleration structures for the scene.
    engine.initAabbBlas();
    for (std::size_t i = 0; i < engine.mScene.mMeshes.size(); ++i) { engine.initMeshBlas(engine.mScene.mMeshes[i]);}
    engine.initSceneTLAS();

    
    engine.initDescriptorSets();
    engine.initComputePipeline();


    engine.render();
    fmt::println("\nSample count: {} * {} = {}", engine.mSamplingParams.mNumSamples, engine.mNumBatches, engine.mSamplingParams.mNumSamples * engine.mNumBatches);
    fmt::println("Recursion depth: {}", engine.mSamplingParams.mNumBounces);


    // Write rendered image to file.
    const fs::path sceneDirectory("../../scenes"); // TODO: Use Command line args to specify out folder.
    const auto outPath = (sceneDirectory / engine.mScene.mName).replace_extension(".hdr");
    engine.writeImage(outPath);
    fmt::println("Image written to: {}", fs::absolute(outPath).string());

    engine.cleanup();
}
