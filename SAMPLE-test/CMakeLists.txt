cmake_minimum_required(VERSION 3.15)

project(meshoptimizer_hotdog_sample LANGUAGES CXX)

# Build meshoptimizer directly from the repository containing this sample. This
# keeps the example self-contained and avoids requiring a global installation.
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/meshoptimizer")

add_executable(hotdog_lossless src/hotdog_lossless.cpp)
target_compile_features(hotdog_lossless PRIVATE cxx_std_11)
target_link_libraries(hotdog_lossless PRIVATE meshoptimizer)

if(MSVC)
    target_compile_options(hotdog_lossless PRIVATE /W4)
else()
    target_compile_options(hotdog_lossless PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()
