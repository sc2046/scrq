add_executable (ch9 
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(ch9 glfw)
target_link_libraries(ch9 vk-bootstrap)
target_link_libraries(ch9 volk)
target_link_libraries(ch9 Vulkan::Vulkan)
target_link_libraries(ch9 fmt)


add_custom_command(
	TARGET ch9 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/ch9
	$<TARGET_FILE_DIR:ch9>/shaders/ch9)
set_property(TARGET ch9 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")