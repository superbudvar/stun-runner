cmake_minimum_required(VERSION 3.16)
project(stunrun_backends C)

set(CMAKE_C_STANDARD 11)

# ---- Compiler warnings ----
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
    )
    # Pass debug flags separately so CMake doesn't emit raw generator expressions
    # into compiler flags (which causes "linker input file not found" errors)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -DDEBUG_BACKEND=1)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    endif()
endif()

# ---- Which SDL generation? ----
#
# A single binary CANNOT link libSDL2 and libSDL3 at the same time.  Both
# export the same symbol names with incompatible signatures -- SDL_Init,
# SDL_CreateWindow, SDL_GetError, SDL_PollEvent, SDL_Delay and the rest all
# changed between the two -- so the linker resolves one set of names to one
# library and every other call site silently lands on the wrong ABI.
#
# We hit exactly that: installing SDL3 on a machine that already had SDL2 made
# SDL3_FOUND true, which suppressed the SDL2 search AND linked libSDL3 into
# stunrun_backends.  native/shell.c includes <SDL2/SDL.h> and owns the window,
# the event loop and the frame pacing, so its calls were dispatched into
# libSDL3.  SDL_Init then failed with an empty error string and SDL_CreateWindow
# passed an SDL2-shaped argument list to an SDL3 entry point, producing a bare
# `X Error of failed request: BadValue ... X_CreateWindow` that killed the
# process before any fallback could run.  The symptom looked like a broken
# X server; the cause was a link-time ABI mixup.
#
# So: detect BOTH independently, then pick exactly one.  SDL2 is the default
# because shell.c -- the main executable path -- is written against SDL2.
# Override with -DSTUNRUN_SDL=3 to build the SDL3 backends instead.
set(STUNRUN_SDL "AUTO" CACHE STRING
    "SDL generation to build against: AUTO (prefer SDL2), 2, or 3")
set_property(CACHE STUNRUN_SDL PROPERTY STRINGS AUTO 2 3)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    pkg_check_modules(SDL3 QUIET sdl3)
endif()
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()
if(NOT SDL3_FOUND)
    find_package(SDL3 QUIET)
endif()
if(NOT SDL3_FOUND AND UNIX)
    find_package(SDL3 QUIET PATHS /usr/lib64 /usr/local/lib64 NO_DEFAULT_PATH)
endif()

if(STUNRUN_SDL STREQUAL "AUTO")
    # Prefer SDL2: native/shell.c is the SDL2 caller.
    if(SDL2_FOUND)
        set(STUNRUN_SDL_RESOLVED 2)
    elseif(SDL3_FOUND)
        set(STUNRUN_SDL_RESOLVED 3)
    else()
        set(STUNRUN_SDL_RESOLVED 0)
    endif()
elseif(STUNRUN_SDL STREQUAL "2")
    set(STUNRUN_SDL_RESOLVED 2)
elseif(STUNRUN_SDL STREQUAL "3")
    set(STUNRUN_SDL_RESOLVED 3)
else()
    message(FATAL_ERROR "STUNRUN_SDL must be AUTO, 2 or 3 (got '${STUNRUN_SDL}')")
endif()

# Collapse so only the chosen generation can satisfy a `*_FOUND` test below.
if(STUNRUN_SDL_RESOLVED EQUAL 2)
    set(SDL3_FOUND OFF)
elseif(STUNRUN_SDL_RESOLVED EQUAL 3)
    set(SDL2_FOUND OFF)
endif()
set(STUNRUN_SDL2_BACKEND_ENABLED ${SDL2_FOUND} PARENT_SCOPE)
set(STUNRUN_GSP_GL_SCANOUT_ENABLED OFF PARENT_SCOPE)

if(STUNRUN_SDL_RESOLVED EQUAL 2)
    message(STATUS "SDL generation: SDL2 ${SDL2_VERSION} (SDL2 found: ${SDL2_FOUND}, SDL3 found: ${SDL3_FOUND} -- ignored)")
elseif(STUNRUN_SDL_RESOLVED EQUAL 3)
    message(STATUS "SDL generation: SDL3 ${SDL3_VERSION} (native/shell.c is still written against SDL2; it needs porting)")
else()
    message(STATUS "SDL generation: none (null backend only)")
endif()

# ---- Backend library ----
add_library(stunrun_backends OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/backend_registry.c
)

# ---- Add backends conditionally based on availability ----

# Null backend: always available (headless / CI)
target_sources(stunrun_backends PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/null_backend.c
)

# SDL2 backend: available if SDL2 is found
if(SDL2_FOUND)
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/sdl_video.c
        ${CMAKE_CURRENT_LIST_DIR}/sdl_audio.c
    )
    target_include_directories(stunrun_backends PUBLIC
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_CURRENT_LIST_DIR}       # backend/ — for "backend.h"
        ${CMAKE_CURRENT_LIST_DIR}/..   # stun-runner root — for "backend/backend.h"
    )
    # GLEW for modern OpenGL (glCreateShader, glGenVertexArrays, etc.)
    find_package(GLEW QUIET)
    find_package(OpenGL QUIET)
    if(GLEW_FOUND)
        target_sources(stunrun_backends PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/gsp_scanout_gl.c
        )
        set(STUNRUN_GSP_GL_SCANOUT_ENABLED ON PARENT_SCOPE)
        target_include_directories(stunrun_backends PUBLIC
            ${GLEW_INCLUDE_DIRS}
        )
        # Explicitly include OpenGL dispatch library (libOpenGL.so / libGL.so).
        # OPENGL_LIBRARIES may be empty on libglvnd systems; use the discovered
        # library path directly.  GLEW's transitive libGL dependency is not
        # automatically pulled in by the linker for static-link ordering reasons.
        if(OPENGL_opengl_LIBRARY)
            target_link_libraries(stunrun_backends PUBLIC
                ${SDL2_LIBRARIES}
                ${GLEW_LIBRARIES}
                ${OPENGL_opengl_LIBRARY}
            )
        else()
            target_link_libraries(stunrun_backends PUBLIC
                ${SDL2_LIBRARIES}
                ${GLEW_LIBRARIES}
            )
        endif()
        message(STATUS "SDL2 backend: enabled (SDL2 ${SDL2_VERSION}, GLEW ${GLEW_VERSION})")
    else()
        # Fallback: no modern GL support, sdl_video.c will build but runtime
        # will fail if GL 2+ is not available through libGL directly
        target_link_libraries(stunrun_backends PUBLIC
            ${SDL2_LIBRARIES}
            ${OPENGL_opengl_LIBRARY}
        )
        message(STATUS "SDL2 backend: enabled (SDL2 ${SDL2_VERSION}, GLEW not found — limited GL support)")
    endif()
else()
    message(STATUS "SDL2 backend: disabled (SDL2 not found)")
endif()

# SDL3 backends: available if SDL3 is found
# SDL3 provides both GPU (Vulkan/Metal/D3D12) and AudioStream APIs.
# These are preferred over SDL2 on platforms that support SDL3.
if(SDL3_FOUND)
    target_include_directories(stunrun_backends PUBLIC
        ${SDL3_INCLUDE_DIRS}
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
    )
    # GLEW for modern OpenGL (glCreateShader, glGenVertexArrays, etc.) on desktop
    find_package(GLEW QUIET)
    find_package(OpenGL QUIET)
    if(GLEW_FOUND)
        target_include_directories(stunrun_backends PUBLIC ${GLEW_INCLUDE_DIRS})
    endif()

    # SDL3 OpenGL video backend (works like sdl_video but with SDL3)
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/sdl3_video.c
    )
    if(GLEW_FOUND AND OPENGL_opengl_LIBRARY)
        target_link_libraries(stunrun_backends PUBLIC
            ${SDL3_LIBRARIES}
            ${GLEW_LIBRARIES}
            ${OPENGL_opengl_LIBRARY}
        )
    else()
        target_link_libraries(stunrun_backends PUBLIC
            ${SDL3_LIBRARIES}
            ${OPENGL_LIBRARIES}
        )
    endif()

    # SDL3 audio backend (AudioStream API — replaces ring-buffer model)
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/sdl3_audio.c
    )
    # (links same libs — no additional dependencies beyond SDL3)

    # SDL3 GPU backend (portable Vulkan/Metal/D3D12 pipeline via SDL_GPUDevice)
    # Requires compiled SPIR-V (Linux/Android), MSL (macOS), or DXIL (Windows) shaders.
    # Build with: -DSDL3_GPU_SHADER_PATH=shaders/ to enable.
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/sdl3_gpu.c
    )

    # Vulkan backend: enable alongside SDL3 (cross-compilation friendly)
    if(Vulkan_FOUND)
        target_sources(stunrun_backends PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/vulkan_backend.c
        )
        target_include_directories(stunrun_backends PUBLIC
            ${Vulkan_INCLUDE_DIRS}
        )
        target_link_libraries(stunrun_backends PUBLIC
            ${Vulkan_LIBRARIES}
        )
    endif()

    message(STATUS "SDL3 backends: enabled (SDL3 ${SDL3_VERSION})")
else()
    # No SDL3: fall back to Vulkan directly if available
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_sources(stunrun_backends PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/vulkan_backend.c
        )
        target_include_directories(stunrun_backends PUBLIC
            ${Vulkan_INCLUDE_DIRS}
        )
        target_link_libraries(stunrun_backends PUBLIC
            ${Vulkan_LIBRARIES}
        )
        message(STATUS "Vulkan backend: enabled (SDL3 not found)")
    else()
        message(STATUS "Vulkan backend: disabled (Vulkan not found)")
    endif()
endif()

# Metal backend: available only on Apple platforms
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/vulkan_backend.c
    )
    target_include_directories(stunrun_backends PUBLIC
        ${Vulkan_INCLUDE_DIRS}
    )
    target_link_libraries(stunrun_backends PUBLIC
        ${Vulkan_LIBRARIES}
    )
    message(STATUS "Vulkan backend: enabled")
else()
    message(STATUS "Vulkan backend: disabled (Vulkan not found)")
endif()

# Metal backend: available only on Apple platforms
if(APPLE)
    target_sources(stunrun_backends PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/metal_backend.m
    )
    target_link_libraries(stunrun_backends PUBLIC
        "-framework Metal" "-framework QuartzCore"
    )
    message(STATUS "Metal backend: enabled")
endif()

# ---- Install ----
install(TARGETS stunrun_backends
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
