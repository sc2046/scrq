add_executable (ch8 
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(ch8 glfw)
target_link_libraries(ch8 vk-bootstrap)
target_link_libraries(ch8 volk)
target_link_libraries(ch8 Vulkan::Vulkan)
target_link_libraries(ch8 fmt)


add_custom_command(
	TARGET ch8 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/ch8
	$<TARGET_FILE_DIR:ch8>/shaders/ch8)
set_property(TARGET ch8 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")