# mame_harness/CMakeLists.txt
# C++ harness that feeds MAME oracle VRAM dumps into the render pipeline

cmake_minimum_required(VERSION 3.20)
project(stunrun_mame_harness LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include paths for the native library headers
get_filename_component(STUNRUN_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../native" ABSOLUTE)
get_filename_component(STUNRUN_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../backend" ABSOLUTE)
get_filename_component(STUNRUN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" ABSOLUTE)

add_executable(test_oracle_vram
  test_oracle_vram.c
  ${STUNRUN_NATIVE_DIR}/gsp_video.c
  ${STUNRUN_NATIVE_DIR}/render.c
)
target_include_directories(test_oracle_vram PRIVATE
  ${STUNRUN_NATIVE_DIR}
  ${STUNRUN_BACKEND_DIR}
  ${STUNRUN_THIRD_PARTY_DIR}/stb
)
target_link_libraries(test_oracle_vram PRIVATE m)
