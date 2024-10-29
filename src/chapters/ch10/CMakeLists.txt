add_executable (ch10
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(ch10 glfw)
target_link_libraries(ch10 vk-bootstrap)
target_link_libraries(ch10 volk)
target_link_libraries(ch10 Vulkan::Vulkan)
target_link_libraries(ch10 fmt)


add_custom_command(
	TARGET ch10 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/ch10
	$<TARGET_FILE_DIR:ch10>/shaders/ch10)
set_property(TARGET ch10 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")