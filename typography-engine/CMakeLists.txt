cmake_minimum_required(VERSION 3.16)
project(typo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_library(typo STATIC src/typo.cpp)
target_include_directories(typo PUBLIC include)
if(MSVC)
	target_compile_options(typo PRIVATE /utf-8)
endif()

# Tests: typo_test <folder of the profiles> <folder of the cases>
option(TYPO_BUILD_TESTS "Build the tests" ON)
if(TYPO_BUILD_TESTS)
	add_executable(typo_test tests/typo_test.cpp)
	target_link_libraries(typo_test PRIVATE typo)
	enable_testing()
	add_test(NAME typo COMMAND typo_test ${CMAKE_CURRENT_SOURCE_DIR}/profiles ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases)
endif()
