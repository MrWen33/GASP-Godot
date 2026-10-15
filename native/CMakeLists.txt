cmake_minimum_required(VERSION 3.20)
project(gasp_native CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(godot-cpp)
file(GLOB SRC CONFIGURE_DEPENDS src/*.cpp)
add_library(gasp_native SHARED ${SRC})
target_link_libraries(gasp_native PRIVATE godot-cpp)
target_compile_options(gasp_native PRIVATE /O2 /Zi /fp:fast /W3 /wd4244 /wd4305 /wd4267 /bigobj)
target_link_options(gasp_native PRIVATE /DEBUG /OPT:REF /OPT:ICF)
set_target_properties(gasp_native PROPERTIES
  OUTPUT_NAME "gasp_native"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../godot/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../godot/bin"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../godot/bin")
