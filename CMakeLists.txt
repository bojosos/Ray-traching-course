cmake_minimum_required(VERSION 3.0.0)

project(RTInOneWeekend)

set(HEADERS
	src/third_party/glad/include/glad/glad.h
	src/third_party/glfw/include/GLFW/glfw3.h
	src/third_party/glfw/include/GLFW/glfw3native.h
	src/third_party/imgui/imgui.h
)

set(SOURCES
	src/Material.h
	src/Material.cpp

	src/Image.hpp

	src/Primitive.h
	src/Primitive.cpp
	src/Accelerators.cpp

	src/Utils.hpp
	src/Threading.hpp
	src/Mesh.h
	src/Mesh.cpp
	src/Framebuffer.cpp
	src/Framebuffer.h
	
	src/Window.h
	src/Window.cpp

	src/Mesh.cpp
	src/main.cpp
	
	src/FileSystem.cpp
	src/FileSystem.h

	src/ImGuiBuild.cpp
	src/ImGui.h

	src/RenderLog.h
	src/Module.h

	src/third_party/glad/src/glad.c
	
	src/third_party/imgui/imgui.cpp
	src/third_party/imgui/imgui_demo.cpp
	src/third_party/imgui/imgui_draw.cpp
	src/third_party/imgui/imgui_widgets.cpp
	src/third_party/imgui/imgui_tables.cpp
	
	src/third_party/imgui/misc/cpp/imgui_stdlib.h
 	src/third_party/imgui/misc/cpp/imgui_stdlib.cpp
)

add_definitions(-D_GLFW_WIN32)
add_executable(${PROJECT_NAME} "${SOURCES};${HEADERS}")
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE
	src/third_party/glad/include
	src/third_party/glfw/include
	src/third_party/imgui
)
target_link_libraries(${PROJECT_NAME} src/third_party/glfw/lib/glfw3)
target_compile_definitions(${PROJECT_NAME} PRIVATE MESH_FOLDER="${CMAKE_SOURCE_DIR}/mesh")
