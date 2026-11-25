cmake_minimum_required(VERSION 3.16)
project(binote LANGUAGES C CXX)
if(WIN32)
	enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Dear ImGui, GLFW, Python and the code editor come from this folder
set(BINOTE_THIRD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../openpostpro/openpostpro" CACHE PATH "Folder of the third party libraries")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third/imgui-1.92.1")
set(IMGUI_MD_IMGUI_DIR ${IMGUI_DIR} CACHE PATH "" FORCE)
set(IMGUI_PROSE_IMGUI_DIR ${IMGUI_DIR} CACHE PATH "" FORCE)
set(IMGUI_MD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(IMGUI_PROSE_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(../imgui-md imgui-md)
add_subdirectory(../imgui-prose imgui-prose)
set(TYPO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../typography-engine typography-engine)

# Hunspell (spelling), compiled with Binote
file(GLOB HUNSPELL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third/hunspell/*.cxx)
add_library(hunspell STATIC ${HUNSPELL_SOURCES})
target_compile_definitions(hunspell PUBLIC HUNSPELL_STATIC)
target_include_directories(hunspell PUBLIC third/hunspell)
if(MSVC)
	target_compile_options(hunspell PRIVATE /w)
endif()

add_executable(binote
	src/main.cpp
	src/ui/bridge.cpp
	src/ui/app.cpp
	src/ui/app_charmap.cpp
	src/ui/app_export.cpp
	src/export/exporter.cpp
	src/ui/app_notes.cpp
	src/ui/app_names.cpp
	src/ui/app_problems.cpp
	src/ui/app_style_extra.cpp
	src/ui/app_script.cpp
	src/ui/app_links.cpp
	src/ui/app_format.cpp
	src/ui/app_vcs.cpp
	src/vcs/vcs.cpp
	src/ui/app_ui.cpp
	src/analysis/grammar.cpp
	src/analysis/lexicon.cpp
	src/common/i18n.cpp
	src/project/project.cpp
	src/script/pyscript.cpp
	src/project/search.cpp
	src/analysis/spell.cpp
	src/analysis/style.cpp
	src/project/text.cpp
	src/common/util.cpp
	src/common/dialog.cpp
	src/common/lang.cpp
	src/common/logger.cpp
	src/common/strings.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../third/ImGuiColorTextEdit/TextEditor.cpp
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
	${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
if(WIN32)
	target_sources(binote PRIVATE binote.rc)
endif()
target_include_directories(binote PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends ${BINOTE_THIRD_DIR}/src/third ${CMAKE_CURRENT_SOURCE_DIR}/../third/ImGuiColorTextEdit)
# ImGuiColorTextEdit (editor of the scripts) is third party code: no warnings
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../third/ImGuiColorTextEdit/TextEditor.cpp PROPERTIES COMPILE_OPTIONS "/w")
# Python scripts: stable ABI (Py_LIMITED_API 3.12), so any Python >= 3.12 installed on the computer works. On Windows python3.dll is loaded when the first script runs.
if(WIN32)
	target_include_directories(binote PRIVATE ${BINOTE_THIRD_DIR}/src/third/python/313)
	target_link_libraries(binote PRIVATE ${BINOTE_THIRD_DIR}/lib/python/python3.lib delayimp)
	target_link_options(binote PRIVATE /DELAYLOAD:python3.dll)
else()
	find_package(Python3 3.12 REQUIRED COMPONENTS Development.Embed)
	target_include_directories(binote PRIVATE ${Python3_INCLUDE_DIRS})
	target_link_libraries(binote PRIVATE Python3::Python)
endif()
target_link_libraries(binote PRIVATE hunspell imgui_md imgui_prose typo ${BINOTE_THIRD_DIR}/lib/glfw/glfw3.lib opengl32 gdi32 shell32 user32 ole32 comdlg32)
if(MSVC)
	target_compile_options(binote PRIVATE /utf-8 /W3 /wd4244 /wd4267 /wd4996 /wd4305)
	target_compile_definitions(binote PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# Resources beside the executable (copied at every build, also when only a resource changed)
add_custom_target(binote_resources ALL
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:binote>/fonts
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/dict $<TARGET_FILE_DIR:binote>/dict
	COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/fetch-dictionaries.ps1 -Output $<TARGET_FILE_DIR:binote>/dict
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/icons $<TARGET_FILE_DIR:binote>/icons
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:binote>/scripts
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lexicon $<TARGET_FILE_DIR:binote>/lexicon
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lang $<TARGET_FILE_DIR:binote>/lang
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../typography-engine/profiles $<TARGET_FILE_DIR:binote>/typography
	COMMAND ${CMAKE_COMMAND} -E remove_directory $<TARGET_FILE_DIR:binote>/grammar-engine
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../grammar-engine/grammar_engine $<TARGET_FILE_DIR:binote>/grammar-engine)
add_dependencies(binote_resources binote)
