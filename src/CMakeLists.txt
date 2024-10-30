# Add source to this project's executable.
add_executable (VkCRT 
"main.cpp"  
"app.cpp" "app.h"  "chapters/book2/app.cpp" "chapters/book2/app.h" "chapters/book2/main.cpp")

target_link_libraries(VkCRT glfw)
target_link_libraries(VkCRT vk-bootstrap)
target_link_libraries(VkCRT volk)
target_link_libraries(VkCRT Vulkan::Vulkan)
target_link_libraries(VkCRT fmt)

add_subdirectory(chapters)

add_custom_command(
	TARGET VkCRT POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders 
	$<TARGET_FILE_DIR:VkCRT>/shaders)
set_property(TARGET VkCRT PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")
