add_executable (ch6 
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(ch6 glfw)
target_link_libraries(ch6 vk-bootstrap)
target_link_libraries(ch6 volk)
target_link_libraries(ch6 Vulkan::Vulkan)
target_link_libraries(ch6 fmt)


add_custom_command(
	TARGET ch6 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/ch6/
	$<TARGET_FILE_DIR:ch6>/shaders/ch6)
set_property(TARGET ch6 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")