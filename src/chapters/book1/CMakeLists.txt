add_executable (book1
"main.cpp"  
"app.cpp" "app.h")

target_link_libraries(book1 glfw)
target_link_libraries(book1 vk-bootstrap)
target_link_libraries(book1 volk)
target_link_libraries(book1 Vulkan::Vulkan)
target_link_libraries(book1 fmt)


add_custom_command(
	TARGET book1 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/book1
	$<TARGET_FILE_DIR:book1>/shaders/book1)
set_property(TARGET book1 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")