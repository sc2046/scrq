add_executable (ch11
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(ch11 glfw)
target_link_libraries(ch11 vk-bootstrap)
target_link_libraries(ch11 volk)
target_link_libraries(ch11 Vulkan::Vulkan)
target_link_libraries(ch11 fmt)


add_custom_command(
	TARGET ch11 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/ch11
	$<TARGET_FILE_DIR:ch11>/shaders/ch11)
set_property(TARGET ch11 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")