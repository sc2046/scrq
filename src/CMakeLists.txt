add_executable (path_tracer
"main.cpp"  
"app.cpp" "app.h" "tiny_obj_loader.cpp")

target_link_libraries(path_tracer glfw)
target_link_libraries(path_tracer vk-bootstrap)
target_link_libraries(path_tracer volk)
target_link_libraries(path_tracer Vulkan::Vulkan)
target_link_libraries(path_tracer fmt)


add_custom_command(
    TARGET path_tracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:path_tracer>/assets"
    COMMENT "Copying assets to bin folder"
)

# Ensure MyVulkanApp depends on compiled shaders
add_dependencies(path_tracer CompileShaders)