cmake_minimum_required (VERSION 3.18)

project(
  "VkCRT"
  VERSION 1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/${name}")
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${name}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>")

find_package(Vulkan REQUIRED)

find_program(GLSL_VALIDATOR glslangValidator 
    HINTS $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/
    REQUIRED
)

include_directories(
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/external/glfw/include"
    "${PROJECT_SOURCE_DIR}/external/fmt/include"
    "${PROJECT_SOURCE_DIR}/external/glm/glm"
    "${PROJECT_SOURCE_DIR}/external/stb_image"
    "${PROJECT_SOURCE_DIR}/external/vk-bootstrap"
    "${PROJECT_SOURCE_DIR}/external/volk/include"
    "${PROJECT_SOURCE_DIR}/external/VulkanMemoryAllocator/include"
    ${Vulkan_INCLUDE_DIR}
)


add_subdirectory("external/glfw")  
add_subdirectory("external/fmt")  
add_subdirectory("external/VulkanMemoryAllocator")
add_subdirectory("external/volk")  
add_subdirectory("external/vk-bootstrap")


add_subdirectory("shaders")
add_subdirectory("src")




