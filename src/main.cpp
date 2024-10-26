#include "app.h"

#include <chrono>

int main()
{
    VulkanApp engine;
    auto begin = std::chrono::high_resolution_clock::now();

    engine.initContext(true);
    engine.initAllocators();
    engine.initMesh();
    engine.initBuffers();
    engine.initBLAS();
    engine.initTLAS();
    engine.initDescriptorSets();
    engine.initComputePipeline();
    auto end = std::chrono::high_resolution_clock::now();
    fmt::println("Setup took: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());


    begin = std::chrono::high_resolution_clock::now();
    engine.run();
    end = std::chrono::high_resolution_clock::now();
    fmt::println("Render took: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());
    engine.cleanup();
}
