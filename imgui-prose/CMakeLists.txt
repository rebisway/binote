cmake_minimum_required(VERSION 3.16)
project(imgui_prose LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Dear ImGui sources (the library only needs imgui.h and imgui_internal.h)
set(IMGUI_PROSE_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third/imgui-1.92.1" CACHE PATH "Dear ImGui folder")

add_library(imgui_prose STATIC src/imgui_prose.cpp)
target_include_directories(imgui_prose PUBLIC include ${IMGUI_PROSE_IMGUI_DIR})

# Demo: GLFW + OpenGL3 window. GLFW headers and library are taken from the third party folder by default.
option(IMGUI_PROSE_BUILD_DEMO "Build the demo" ON)
if(IMGUI_PROSE_BUILD_DEMO)
	set(IMGUI_PROSE_GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../openpostpro/openpostpro" CACHE PATH "Folder holding src/third/GLFW and lib/glfw")
	set(IMGUI_PROSE_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../binote-app/fonts" CACHE PATH "Folder of the demo fonts (DejaVu)")
	add_executable(imgui_prose_demo
		demo/main.cpp
		${IMGUI_PROSE_IMGUI_DIR}/imgui.cpp
		${IMGUI_PROSE_IMGUI_DIR}/imgui_draw.cpp
		${IMGUI_PROSE_IMGUI_DIR}/imgui_tables.cpp
		${IMGUI_PROSE_IMGUI_DIR}/imgui_widgets.cpp
		${IMGUI_PROSE_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
		${IMGUI_PROSE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
	target_include_directories(imgui_prose_demo PRIVATE ${IMGUI_PROSE_IMGUI_DIR} ${IMGUI_PROSE_IMGUI_DIR}/backends ${IMGUI_PROSE_GLFW_DIR}/src/third)
	target_compile_definitions(imgui_prose_demo PRIVATE IMGUI_PROSE_FONT_DIR="${IMGUI_PROSE_FONT_DIR}")
	target_link_libraries(imgui_prose_demo PRIVATE imgui_prose ${IMGUI_PROSE_GLFW_DIR}/lib/glfw/glfw3.lib opengl32 gdi32 shell32 user32)
endif()
