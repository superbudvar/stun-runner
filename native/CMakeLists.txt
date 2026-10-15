cmake_minimum_required(VERSION 3.20)
project(stunrun_native_smoke LANGUAGES C CXX)

# Link the Verilated TMS34010 core into the shell. Off by default; see the
# guard below for why the ROM-free build must not require a simulator.
option(STUNRUN_GSP_RTL
  "Link the Verilated GSP core into the shell (requires Verilator)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Backend library (SDL2 / Vulkan / Metal / null) ----
add_subdirectory(../backend backend_build)

# ---- ymfm library (YM2151 FM synthesis) ----------------------------------
# BSD-3-Clause, Aaron Giles.  Compiling all ymfm sources since internal
# dependencies (ssg_engine, adpcm_engine) are linked by misc.cpp.
file(GLOB YMFM_ALL_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ymfm/src/ymfm_*.cpp
)
set(YMFM_OPM_SOURCES ${YMFM_ALL_SRC})
set(YMFM_INC
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ymfm/src
)

add_executable(stunrun-native-smoke smoke.cpp)
# Agent 2 M3 slice: the native target consumes the same C init-slice
# sources as the reproduction target (single canonical implementation,
# compiled into both). C99, no extra dependencies.
add_executable(stunrun-adsp-init-slice-test
  ../reproduction/adsp/adsp_init_image.c
  ../reproduction/adsp/adsp_init_image_test.c)
target_include_directories(stunrun-adsp-init-slice-test PRIVATE
  ../reproduction/adsp)
set_target_properties(stunrun-adsp-init-slice-test PROPERTIES C_STANDARD 99)
add_executable(stunrun-adsp-image-dump
  ../reproduction/adsp/adsp_init_image.c
  ../reproduction/adsp/adsp_image_dump.c)
target_include_directories(stunrun-adsp-image-dump PRIVATE
  ../reproduction/adsp)
set_target_properties(stunrun-adsp-image-dump PROPERTIES C_STANDARD 99)
# Agent 2 M3 slice 2: 68010-visible ADSP control/IRQ contract plus the
# upload/install frame boundary. Same single-implementation rule as above.
add_executable(stunrun-adsp-control-slice-test
  ../reproduction/adsp/adsp_control_seq.c
  ../reproduction/adsp/adsp_control_seq_test.c)
target_include_directories(stunrun-adsp-control-slice-test PRIVATE
  ../reproduction/adsp)
set_target_properties(stunrun-adsp-control-slice-test PROPERTIES C_STANDARD 99)
# Agent 2 M3 slice 3: 68010-side ADSP program-upload stream framing (17
# records, destination gap, 0xFF terminator). Same rule as above.
add_executable(stunrun-adsp-upload-slice-test
  ../reproduction/adsp/adsp_upload_stream.c
  ../reproduction/adsp/adsp_upload_stream_test.c)
target_include_directories(stunrun-adsp-upload-slice-test PRIVATE
  ../reproduction/adsp)
set_target_properties(stunrun-adsp-upload-slice-test PROPERTIES C_STANDARD 99)
enable_testing()
add_test(NAME native-smoke COMMAND stunrun-native-smoke)
add_executable(stunrun-fullscreen-quad-test
  ../backend/fullscreen_quad_test.c)
target_include_directories(stunrun-fullscreen-quad-test PRIVATE ../backend)
set_target_properties(stunrun-fullscreen-quad-test PROPERTIES C_STANDARD 99)
add_test(NAME backend-fullscreen-quad-c COMMAND stunrun-fullscreen-quad-test)
add_executable(stunrun-fake-ports-test fake_ports.c fake_ports_test.c)
target_include_directories(stunrun-fake-ports-test PRIVATE .)
set_target_properties(stunrun-fake-ports-test PROPERTIES C_STANDARD 99)
add_test(NAME fake-ports-slice-c COMMAND stunrun-fake-ports-test)
add_executable(stunrun-mc68010-text-writer-test
  mc68010_text_writer.c mc68010_text_writer_test.c)
set_target_properties(stunrun-mc68010-text-writer-test PROPERTIES C_STANDARD 99)
add_test(NAME mc68010-text-writer-slice-c COMMAND stunrun-mc68010-text-writer-test)
# Agent 1 (this session): road FIFO write routines at $22EC-$24B8.
add_executable(stunrun-mc68010-road-writer-test
  mc68010_road_writer.c mc68010_road_writer_test.c)
target_include_directories(stunrun-mc68010-road-writer-test PRIVATE .)
set_target_properties(stunrun-mc68010-road-writer-test PROPERTIES C_STANDARD 99)
add_test(NAME mc68010-road-writer-slice-c COMMAND stunrun-mc68010-road-writer-test)
# Agent 1 (this session): text selector dispatch at $196 and text-from-descriptor at $2580.
add_executable(stunrun-mc68010-text-selector-writer-test
  mc68010_text_selector_writer.c mc68010_text_selector_writer_test.c)
target_include_directories(stunrun-mc68010-text-selector-writer-test PRIVATE .)
set_target_properties(stunrun-mc68010-text-selector-writer-test PROPERTIES C_STANDARD 99)
add_test(NAME mc68010-text-selector-writer-slice-c COMMAND stunrun-mc68010-text-selector-writer-test)
add_test(NAME adsp-init-slice-c COMMAND stunrun-adsp-init-slice-test)
add_test(NAME adsp-control-slice-c COMMAND stunrun-adsp-control-slice-test)
add_test(NAME adsp-upload-slice-c COMMAND stunrun-adsp-upload-slice-test)
# Agent 2 sound slice: JSA-II command/response latch transport (68010 <->
# 6502). Same single-implementation rule as the ADSP slices above.
add_executable(stunrun-jsa-latch-slice-test
  ../reproduction/sound/jsa_latch.c
  ../reproduction/sound/jsa_latch_test.c)
target_include_directories(stunrun-jsa-latch-slice-test PRIVATE
  ../reproduction/sound)
set_target_properties(stunrun-jsa-latch-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME jsa-latch-slice-c COMMAND stunrun-jsa-latch-slice-test)
# JSA-II 68010 sound-command -> OKI6295 translation. Transcription of the
# 6502 firmware's $6100/$616D/$6532/$6567/$659C/$5C63/$5C67/$5C6B tables and
# of the class-19 play ($5BFE), class-20 stop ($5C6F) and class-0 $1E gate
# ($438A) handlers.  The test re-extracts every table from
# roms/stunrun/136070-2123.10c when it is present and skips that part when it
# is not, so the CTest suite stays runnable without ROMs.
add_executable(stunrun-oki-cmdmap-slice-test
  ../reproduction/sound/oki_cmdmap.c
  ../reproduction/sound/oki_cmdmap_test.c)
target_include_directories(stunrun-oki-cmdmap-slice-test PRIVATE
  ../reproduction/sound)
set_target_properties(stunrun-oki-cmdmap-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME oki-cmdmap-slice-c COMMAND stunrun-oki-cmdmap-slice-test)
# OKI6295 ADPCM decoder.  Replaces the IMA codec, the reversed volume table,
# the low-nibble-first read order, the start==stop acceptance and the 1:1 mix
# with MAME's oki_adpcm_state + okim6295 behaviour.  The test asserts decoded
# PCM values, both clamps, the volume table's orientation, the PIN7 divisor
# and, when the four OKI sample ROMs are present, a whole real sample.
add_executable(stunrun-oki6295-slice-test
  ../reproduction/sound/oki6295.c
  ../reproduction/sound/oki6295_test.c)
target_include_directories(stunrun-oki6295-slice-test PRIVATE
  ../reproduction/sound)
set_target_properties(stunrun-oki6295-slice-test PROPERTIES C_STANDARD 99)
# oki6295_test.c re-derives the oracle's stepval table with pow()/floor().
if(NOT MSVC)
  target_link_libraries(stunrun-oki6295-slice-test m)
endif()
add_test(NAME oki6295-slice-c COMMAND stunrun-oki6295-slice-test)
# The dispatch bridge: the 68010 command byte -> oki_cmdmap -> OKI6295 ->
# audio backend path.  Resolves findings-doc D2 (the raw command byte was fed
# straight to oki6295_write()) and D15 (the latch was peeked, not consumed, so
# one command re-dispatched every frame).  The test drives a stub
# stunrun_audio_backend_t and asserts the OKI byte count, the voice the chip
# started, the exactly-once behaviour over eight frames, and the PCM that
# reaches the backend.
add_executable(stunrun-sound-dispatch-slice-test
  ../reproduction/sound/jsa_latch.c
  ../reproduction/sound/oki6295.c
  ../reproduction/sound/oki_cmdmap.c
  ../reproduction/sound/ym2151.c
  ../reproduction/sound/ymfm_chip.cpp
  ../reproduction/sound/sound_dispatch.c
  ../reproduction/sound/sound_dispatch_test.c
  ${YMFM_OPM_SOURCES})
target_include_directories(stunrun-sound-dispatch-slice-test PRIVATE
  ../reproduction/sound ${YMFM_INC})
set_target_properties(stunrun-sound-dispatch-slice-test PROPERTIES
  C_STANDARD 99 CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
add_test(NAME sound-dispatch-slice-c COMMAND stunrun-sound-dispatch-slice-test)
# 68010 sound-command queue ($301CA enqueue / $3013C drain) feeding
# the JSA latch transport; resolves the jsa_latch.h producer-buffer
# open item on the 68010 side.
# NOTE: neither sound_queue.c nor sound_queue_test.c exists yet, so this
# block is currently inert.  It is left in place (rather than deleted) as the
# landing spot for that slice, and because the EXISTS guard is the same idiom
# used by the other optional slices below.  It costs nothing: an unmet guard
# means no target and no test.
if(EXISTS ../reproduction/sound/sound_queue.c)
  add_executable(stunrun-sound-queue-slice-test
    ../reproduction/sound/sound_queue.c
    ../reproduction/sound/sound_queue_test.c)
  target_include_directories(stunrun-sound-queue-slice-test PRIVATE
    ../reproduction/sound)
  set_target_properties(stunrun-sound-queue-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME sound-queue-slice-c COMMAND stunrun-sound-queue-slice-test)
endif()
# Agent 2 M4 step 1: deterministic native shell. Fixed 600-frame walk over
# the five verified slices, checkpoint on stdout, exit 0 iff all asserts
# hold. No speculative game logic.
add_executable(stunrun-native-shell
  shell.c
  fake_ports.c
  musashi_cpu.c
  jsa_transport.c
  gsp_display_list.cpp
  gsp_vram_clear.c
  musashi_loader.c
  cpu_override.c
  game_loop.c
  road_strip.c
  ../reproduction/maincpu/trajectory_state.c
  ../reproduction/maincpu/geom_upload.c
  ../reproduction/maincpu/road_fifo.c
  render.c
  gsp_display.c
  gsp_video.c
  checkpoint.c
  experiment.c
  ../reproduction/adsp/adsp_init_image.c
  ../reproduction/adsp/adsp_control_seq.c
  ../reproduction/adsp/adsp_upload_stream.c
  ../reproduction/maincpu/fifo_block.c
  ../reproduction/gsp/text_cursor.c
  ../reproduction/gsp/text_record.c
  ../reproduction/sound/jsa_latch.c
  ../reproduction/sound/oki6295.c
  ../reproduction/sound/oki_cmdmap.c
  ../reproduction/sound/ym2151.c
  ../reproduction/sound/ymfm_chip.cpp
  ../reproduction/sound/sound_dispatch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c
  ${YMFM_OPM_SOURCES})
target_link_libraries(stunrun-native-shell PRIVATE
  m
  stunrun_backends
  stunrun_gsp_board
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
target_include_directories(stunrun-native-shell PRIVATE
  . ../reproduction/adsp ../reproduction/maincpu ../reproduction/gsp
  ../reproduction/sound ../backend
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ymfm/src
  ${CMAKE_CURRENT_SOURCE_DIR}/gsp_rtl)
set_target_properties(stunrun-native-shell PROPERTIES
  C_STANDARD 99 CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

if(STUNRUN_GSP_GL_SCANOUT_ENABLED)
  target_compile_definitions(stunrun-native-shell PRIVATE
    STUNRUN_GSP_GL_SCANOUT=1)
  add_executable(stunrun-gsp-scanout-gl-test
    gsp_scanout_gl_test.c gsp_display.c render.c)
  target_link_libraries(stunrun-gsp-scanout-gl-test PRIVATE stunrun_backends)
  target_include_directories(stunrun-gsp-scanout-gl-test PRIVATE
    . ../backend)
  set_target_properties(stunrun-gsp-scanout-gl-test PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON)
  add_test(NAME gsp-scanout-gl-parity COMMAND stunrun-gsp-scanout-gl-test)
  set_tests_properties(gsp-scanout-gl-parity PROPERTIES SKIP_RETURN_CODE 77)
endif()
add_test(NAME native-shell COMMAND stunrun-native-shell)
set_tests_properties(native-shell PROPERTIES
  ENVIRONMENT "STUNRUN_BACKEND=null;STUNRUN_VIDEO_BACKEND=null;STUNRUN_AUDIO_BACKEND=null")

add_executable(stunrun-gsp-vram-clear-test gsp_vram_clear.c gsp_vram_clear_test.c)
set_target_properties(stunrun-gsp-vram-clear-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-vram-clear-slice-c COMMAND stunrun-gsp-vram-clear-test)

add_executable(stunrun-gsp-word-pair-test gsp_word_pair.c gsp_word_pair_test.c)
set_target_properties(stunrun-gsp-word-pair-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-word-pair-slice-c COMMAND stunrun-gsp-word-pair-test)

add_executable(stunrun-gsp-2bpp-glyph-test gsp_2bpp_glyph.c gsp_2bpp_glyph_test.c)
set_target_properties(stunrun-gsp-2bpp-glyph-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-2bpp-glyph-slice-c COMMAND stunrun-gsp-2bpp-glyph-test)

# Agent 2: MC68010 object record update routines
add_executable(stunrun-mc68010-object-writer-test
  mc68010_object_writer.c mc68010_object_writer_test.c)
set_target_properties(stunrun-mc68010-object-writer-test PROPERTIES C_STANDARD 99)
add_test(NAME mc68010-object-writer-slice-c COMMAND stunrun-mc68010-object-writer-test)

# Agent 2: Vehicle physics (speed update, input handling, trajectory)
add_executable(stunrun-mc68010-physics-test
  mc68010_physics.c mc68010_physics_test.c)
set_target_properties(stunrun-mc68010-physics-test PROPERTIES C_STANDARD 99)
add_test(NAME mc68010-physics-slice-c COMMAND stunrun-mc68010-physics-test)

# Agent 2: 3D track viewer - extracts track geometry from ROM and renders in OpenGL
find_package(OpenGL QUIET)
if(OpenGL_FOUND)
  add_executable(stunrun-track-viewer-3d track_viewer_3d.c)
  target_link_libraries(stunrun-track-viewer-3d PRIVATE m GL GLU glut)
  target_include_directories(stunrun-track-viewer-3d PRIVATE .)
  target_compile_definitions(stunrun-track-viewer-3d PRIVATE USE_OPENGL)
  set_target_properties(stunrun-track-viewer-3d PROPERTIES C_STANDARD 99)
else()
  # Build without OpenGL if GL is not available
  add_executable(stunrun-track-viewer-3d track_viewer_3d.c)
  target_link_libraries(stunrun-track-viewer-3d PRIVATE m)
  target_include_directories(stunrun-track-viewer-3d PRIVATE .)
  set_target_properties(stunrun-track-viewer-3d PROPERTIES C_STANDARD 99)
endif()

add_executable(stunrun-geometry-fifo-emitter-test
  geometry_fifo_emitter.cpp geometry_fifo_emitter_test.cpp)
set_target_properties(stunrun-geometry-fifo-emitter-test PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME geometry-fifo-emitter-cpp COMMAND stunrun-geometry-fifo-emitter-test)

# Exercise the complete real-ROM OKI -> PCM -> SDL2 backend path when both the
# SDL2 backend and local ROM set are available. Dummy audio keeps this headless.
if(STUNRUN_SDL2_BACKEND_ENABLED
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun/136070-2123.10c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun/136070-2124.1fh"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun/136070-2125.1ef"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun/136070-2126.1de"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun/136070-2127.1cd")
  add_test(NAME native-shell-oki-audio-probe COMMAND stunrun-native-shell)
  set_tests_properties(native-shell-oki-audio-probe PROPERTIES
    ENVIRONMENT
      "SDL_AUDIODRIVER=dummy;STUNRUN_VIDEO_BACKEND=null;STUNRUN_AUDIO_BACKEND=sdl_audio;STUNRUN_AUDIO_PROBE_COMMAND=0x38;STUNRUN_STUNRUN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../roms/stunrun"
    PASS_REGULAR_EXPRESSION "samples-enqueued=[1-9][0-9]*")
endif()

add_executable(stunrun-game-loop-test
  fake_ports.c
  game_loop.c
  ../reproduction/maincpu/trajectory_state.c
  ../reproduction/maincpu/geom_upload.c
  ../reproduction/maincpu/road_fifo.c
  game_loop_test.c)
set_target_properties(stunrun-game-loop-test PROPERTIES C_STANDARD 99)
target_include_directories(stunrun-game-loop-test PRIVATE . ../reproduction/maincpu)
add_test(NAME native-game-loop-c COMMAND stunrun-game-loop-test)
add_executable(stunrun-render-test render.c render_test.c)
set_target_properties(stunrun-render-test PROPERTIES C_STANDARD 99)
add_test(NAME native-render-boundary-c COMMAND stunrun-render-test)
add_executable(stunrun-road-strip-test road_strip.c render.c road_strip_test.c)
set_target_properties(stunrun-road-strip-test PROPERTIES C_STANDARD 99)
target_link_libraries(stunrun-road-strip-test PRIVATE m)
add_test(NAME native-road-strip-c COMMAND stunrun-road-strip-test)
if(EXISTS tunnel_rib_test.c)
  add_executable(stunrun-tunnel-rib-test tunnel_rib.c road_strip.c render.c tunnel_rib_test.c)
  set_target_properties(stunrun-tunnel-rib-test PROPERTIES C_STANDARD 99)
  target_link_libraries(stunrun-tunnel-rib-test PRIVATE m)
  add_test(NAME native-tunnel-rib-c COMMAND stunrun-tunnel-rib-test)
endif()
if(EXISTS vista_quad.c)
  add_executable(stunrun-vista-quad-test vista_quad.c road_strip.c render.c vista_quad_test.c)
  set_target_properties(stunrun-vista-quad-test PROPERTIES C_STANDARD 99)
  target_link_libraries(stunrun-vista-quad-test PRIVATE m)
  add_test(NAME native-vista-quad-c COMMAND stunrun-vista-quad-test)
endif()
add_executable(stunrun-torus-lines-test torus_lines.c render.c torus_lines_test.c)
set_target_properties(stunrun-torus-lines-test PROPERTIES C_STANDARD 99)
target_link_libraries(stunrun-torus-lines-test PRIVATE m)
add_test(NAME native-torus-lines-c COMMAND stunrun-torus-lines-test)
add_executable(stunrun-craft-test craft.c road_strip.c render.c craft_test.c)
set_target_properties(stunrun-craft-test PROPERTIES C_STANDARD 99)
target_link_libraries(stunrun-craft-test PRIVATE m)
add_test(NAME native-craft-c COMMAND stunrun-craft-test)
if(EXISTS vista_quad.c)
  add_executable(stunrun-scene-test scene.c vista_quad.c tunnel_rib.c craft.c road_strip.c render.c scene_test.c)
  set_target_properties(stunrun-scene-test PROPERTIES C_STANDARD 99)
  target_link_libraries(stunrun-scene-test PRIVATE m)
  add_test(NAME native-scene-c COMMAND stunrun-scene-test)
endif()
find_package(OpenGL QUIET)
if (OpenGL_FOUND)
  add_library(stunrun-opengl-backend STATIC opengl_backend.c)
  target_include_directories(stunrun-opengl-backend PRIVATE .)
  target_link_libraries(stunrun-opengl-backend PRIVATE OpenGL::GL)
  set_target_properties(stunrun-opengl-backend PROPERTIES C_STANDARD 99)
endif()
find_package(PkgConfig QUIET)
if (OpenGL_FOUND AND PkgConfig_FOUND)
  pkg_check_modules(GLFW3 QUIET glfw3)
  if (GLFW3_FOUND AND EXISTS stage.c AND EXISTS vehicle.c)
add_executable(stunrun-opengl-demo
      opengl_demo.c
      stage.c
      vehicle.c
      gsp_video.c
      ../reproduction/maincpu/score.c
      ../reproduction/maincpu/award_path.c
      mc68010_physics.c
      fake_ports.c
      game_loop.c
      road_strip.c
      render.c
      ../reproduction/maincpu/trajectory_state.c
      ../reproduction/maincpu/geom_upload.c
      ../reproduction/maincpu/road_fifo.c)
    target_include_directories(stunrun-opengl-demo PRIVATE .
      ../reproduction/maincpu ../reproduction/sound ${GLFW3_INCLUDE_DIRS})
    target_compile_options(stunrun-opengl-demo PRIVATE ${GLFW3_CFLAGS_OTHER})
    target_link_libraries(stunrun-opengl-demo PRIVATE
      stunrun-opengl-backend ${GLFW3_LIBRARIES} OpenGL::GL m)
    set_target_properties(stunrun-opengl-demo PROPERTIES C_STANDARD 99)
  endif()
endif()
if(EXISTS stage.c AND EXISTS vehicle.c)
  add_executable(stunrun-stage-test
    stage.c
    fake_ports.c
    game_loop.c
    road_strip.c
    vehicle.c
    render.c
    gsp_video.c
    ../reproduction/maincpu/score.c
    ../reproduction/maincpu/award_path.c
    mc68010_physics.c
    ../reproduction/maincpu/trajectory_state.c
    ../reproduction/maincpu/geom_upload.c
    ../reproduction/maincpu/road_fifo.c
    stage_test.c)
  target_include_directories(stunrun-stage-test PRIVATE . ../reproduction/maincpu
    ../reproduction/sound)
  target_link_libraries(stunrun-stage-test PRIVATE m)
  set_target_properties(stunrun-stage-test PROPERTIES C_STANDARD 99)
  add_test(NAME native-stage-c COMMAND stunrun-stage-test)
endif()
add_executable(stunrun-gsp-video-test gsp_video.c render.c gsp_video_test.c)
set_target_properties(stunrun-gsp-video-test PROPERTIES C_STANDARD 99)
add_test(NAME native-gsp-video-boundary-c COMMAND stunrun-gsp-video-test)
# Literal GSP indexed-record addressing and raw field positions from the M5
# runtime parser trace. No field semantics are assigned here.
add_executable(stunrun-gsp-record-parser-slice-test
  ../reproduction/gsp/record_parser.c
  ../reproduction/gsp/record_parser_test.c)
target_include_directories(stunrun-gsp-record-parser-slice-test PRIVATE
  ../reproduction/gsp)
set_target_properties(stunrun-gsp-record-parser-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-record-parser-slice-c COMMAND stunrun-gsp-record-parser-slice-test)
# GSP record control-word dispatch from the race-1800 walk decode: bit 15
# skips, bit 12 takes the perspective-sprite path, else the FILL worker.
if(EXISTS ../reproduction/gsp/record_dispatch.c)
  add_executable(stunrun-gsp-record-dispatch-slice-test
    ../reproduction/gsp/record_dispatch.c
    ../reproduction/gsp/record_dispatch_test.c)
  target_include_directories(stunrun-gsp-record-dispatch-slice-test PRIVATE
    ../reproduction/gsp)
  set_target_properties(stunrun-gsp-record-dispatch-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME gsp-record-dispatch-slice-c
    COMMAND stunrun-gsp-record-dispatch-slice-test)
endif()
# Literal GSP FIFO-fed work-buffer setup from the M5 consumer trace.  This
# preserves the masked selector, 16-byte stride, and byte-lane operation
# without assigning semantics to either destination buffer.
add_executable(stunrun-gsp-work-buffer-consumer-slice-test
  ../reproduction/gsp/work_buffer_consumer.c
  ../reproduction/gsp/work_buffer_consumer_test.c)
target_include_directories(stunrun-gsp-work-buffer-consumer-slice-test PRIVATE
  ../reproduction/gsp)
set_target_properties(stunrun-gsp-work-buffer-consumer-slice-test PROPERTIES
  C_STANDARD 99)
add_test(NAME gsp-work-buffer-consumer-slice-c
  COMMAND stunrun-gsp-work-buffer-consumer-slice-test)
# Literal GSP PIXBLT descriptor lane/cursor state from the M5 register tap.
add_executable(stunrun-gsp-text-cursor-slice-test
  ../reproduction/gsp/text_cursor.c
  ../reproduction/gsp/text_cursor_test.c)
target_include_directories(stunrun-gsp-text-cursor-slice-test PRIVATE
  ../reproduction/gsp)
set_target_properties(stunrun-gsp-text-cursor-slice-test PROPERTIES
  C_STANDARD 99)
add_test(NAME gsp-text-cursor-slice-c
  COMMAND stunrun-gsp-text-cursor-slice-test)
# Literal 8-word GSP text-record header/coordinate to descriptor-cursor setup.
add_executable(stunrun-gsp-text-record-slice-test
  ../reproduction/gsp/text_record.c
  ../reproduction/gsp/text_record_test.c)
target_include_directories(stunrun-gsp-text-record-slice-test PRIVATE
  ../reproduction/gsp)
set_target_properties(stunrun-gsp-text-record-slice-test PROPERTIES
  C_STANDARD 99)
add_test(NAME gsp-text-record-slice-c
  COMMAND stunrun-gsp-text-record-slice-test)
# arcade-experiment/v1 parser self-check (valid, corrupt, and schema-
# violating fixtures; no ROMs or MAME involved).
add_executable(stunrun-experiment-parser-test
  experiment.c
  experiment_test.c)
set_target_properties(stunrun-experiment-parser-test PROPERTIES C_STANDARD 99)
add_test(NAME experiment-parser-c COMMAND stunrun-experiment-parser-test)
# File-driven replay of the canonical coin_start experiment through the
# shell: parse, frame dispatch, anchors, and checkpoint must all hold.
add_test(NAME native-shell-coin-start
  COMMAND stunrun-native-shell ${CMAKE_CURRENT_SOURCE_DIR}/../experiments/stunrun/coin_start.json)
set_tests_properties(native-shell-coin-start PROPERTIES
  ENVIRONMENT "STUNRUN_BACKEND=null;STUNRUN_VIDEO_BACKEND=null;STUNRUN_AUDIO_BACKEND=null")
# stunrun-checkpoint/v1 emitter contract (golden comparison lives in
# tests/test_checkpoint_golden.py against the M1 oracle fixture).
add_executable(stunrun-checkpoint-test
  checkpoint.c
  checkpoint_test.c)
set_target_properties(stunrun-checkpoint-test PROPERTIES C_STANDARD 99)
add_test(NAME checkpoint-emitter-c COMMAND stunrun-checkpoint-test)
# Agent 2 maincpu gameplay coverage: live-score accumulator, award
# selection, and hit counter against the weapon_probe/center golden runs.
add_executable(stunrun-score-slice-test
  ../reproduction/maincpu/score.c
  ../reproduction/maincpu/score_test.c)
target_include_directories(stunrun-score-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-score-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME score-slice-c COMMAND stunrun-score-slice-test)
# Persistent NVRAM high-score table decoder against the 10-entry golden
# table (score bytes exact; name prefix only — padding unestablished).
add_executable(stunrun-nvram-scores-slice-test
  ../reproduction/maincpu/nvram_scores.c
  ../reproduction/maincpu/nvram_scores_test.c)
target_include_directories(stunrun-nvram-scores-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-nvram-scores-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME nvram-scores-slice-c COMMAND stunrun-nvram-scores-slice-test)
# Level-select countdown/decay against the boot_coin_start screenshot
# cadence (digit law and race-start expiry observed; backing cell open).
if(EXISTS ../reproduction/maincpu/select_countdown.c)
  add_executable(stunrun-select-countdown-slice-test
    ../reproduction/maincpu/select_countdown.c
    ../reproduction/maincpu/select_countdown_test.c)
  target_include_directories(stunrun-select-countdown-slice-test PRIVATE
    ../reproduction/maincpu)
  set_target_properties(stunrun-select-countdown-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME select-countdown-slice-c
    COMMAND stunrun-select-countdown-slice-test)
endif()
# Red-star collectible counter against the human star-run golden endpoint
# (84 pickups; increment/clear PCs attributed, detection upstream open).
if(EXISTS ../reproduction/maincpu/star_counter.c)
  add_executable(stunrun-star-counter-slice-test
    ../reproduction/maincpu/star_counter.c
    ../reproduction/maincpu/star_counter_test.c)
  target_include_directories(stunrun-star-counter-slice-test PRIVATE
    ../reproduction/maincpu)
  set_target_properties(stunrun-star-counter-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME star-counter-slice-c
    COMMAND stunrun-star-counter-slice-test)
endif()
# Collision verdict (AABB proximity) from the $3B02C listing decode:
# band scaling, conditional grace margins, strict-greater-than.
if(EXISTS ../reproduction/maincpu/collision.c)
  add_executable(stunrun-collision-slice-test
    ../reproduction/maincpu/collision.c
    ../reproduction/maincpu/collision_test.c)
  target_include_directories(stunrun-collision-slice-test PRIVATE
    ../reproduction/maincpu)
  set_target_properties(stunrun-collision-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME collision-slice-c
    COMMAND stunrun-collision-slice-test)
endif()
# ADSP-buffer to GSP-FIFO block framing and transfer-shape contract.
add_executable(stunrun-fifo-block-slice-test
  ../reproduction/maincpu/fifo_block.c
  ../reproduction/maincpu/fifo_block_test.c)
target_include_directories(stunrun-fifo-block-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-fifo-block-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME fifo-block-slice-c COMMAND stunrun-fifo-block-slice-test)
# Literal trajectory coordinate initialization and clamp mechanism from the
# 0x0387C6-0x038A14 listing slice.
add_executable(stunrun-trajectory-state-slice-test
  ../reproduction/maincpu/trajectory_state.c
  ../reproduction/maincpu/trajectory_state_test.c)
target_include_directories(stunrun-trajectory-state-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-trajectory-state-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME trajectory-state-slice-c COMMAND stunrun-trajectory-state-slice-test)
# Literal motion-field branch order from the 0x03ABDE update path. Caller
# inputs remain mechanism-level until their producers are traced.
add_executable(stunrun-motion-update-slice-test
  ../reproduction/maincpu/motion_update.c
  ../reproduction/maincpu/motion_update_test.c)
target_include_directories(stunrun-motion-update-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-motion-update-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME motion-update-slice-c COMMAND stunrun-motion-update-slice-test)
# Course-table march and twin-buffer fan-out against the traced upload
# mechanism (384-word +2 march, count-gated dual copy, 0x300 stride).
add_executable(stunrun-geom-upload-slice-test
  ../reproduction/maincpu/geom_upload.c
  ../reproduction/maincpu/geom_upload_test.c)
target_include_directories(stunrun-geom-upload-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-geom-upload-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME geom-upload-slice-c COMMAND stunrun-geom-upload-slice-test)
add_executable(stunrun-road-fifo-slice-test
  ../reproduction/maincpu/road_fifo.c
  ../reproduction/maincpu/road_fifo_test.c)
target_include_directories(stunrun-road-fifo-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-road-fifo-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME road-fifo-slice-c COMMAND stunrun-road-fifo-slice-test)
add_executable(stunrun-road-buffer-math-slice-test
  ../reproduction/maincpu/road_buffer_math.c
  ../reproduction/maincpu/road_buffer_math_test.c)
target_include_directories(stunrun-road-buffer-math-slice-test PRIVATE
  ../reproduction/maincpu)
set_target_properties(stunrun-road-buffer-math-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME road-buffer-math-slice-c COMMAND stunrun-road-buffer-math-slice-test)
# Object-hit -> award-select -> score-add chain with instruction-stream
# immediates (0x1F4 at 0x3A2E6, dest words 0xFFDD02/0xFF9532).
add_executable(stunrun-award-path-slice-test
  ../reproduction/maincpu/award_path.c
  ../reproduction/maincpu/award_path_test.c
  mc68010_physics.c)
target_include_directories(stunrun-award-path-slice-test PRIVATE
  . ../reproduction/maincpu)
set_target_properties(stunrun-award-path-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME award-path-slice-c COMMAND stunrun-award-path-slice-test)
# Object-record slot template ($03CEFA immediates) and per-frame
# peak/chase servo ($03D1A0) feeding the collision record fields.
if(EXISTS ../reproduction/maincpu/object_record.c)
  add_executable(stunrun-object-record-slice-test
    ../reproduction/maincpu/object_record.c
    ../reproduction/maincpu/object_record_test.c)
  target_include_directories(stunrun-object-record-slice-test PRIVATE
    ../reproduction/maincpu)
  set_target_properties(stunrun-object-record-slice-test PROPERTIES C_STANDARD 99)
  add_test(NAME object-record-slice-c
    COMMAND stunrun-object-record-slice-test)
endif()
# Frame diff tool: compares two PNG/PPM images and reports metrics.
# Test 1: identical frames → exit 0 (tool returns 0). Test 2: modified frame
# → tool returns 1 (max_dist 255 >= threshold).  The "different" case uses a
# threshold of 0 so even a 1-pixel difference causes the tool to return 1,
# which CTest expects (WILL_FAIL).  This documents that the tool correctly
# signals "images differ" for genuinely different frames.
add_executable(stunrun-frame-diff-test ../tools/frame_diff.c)
set_target_properties(stunrun-frame-diff-test PROPERTIES C_STANDARD 99)
target_link_libraries(stunrun-frame-diff-test PRIVATE m)
target_include_directories(stunrun-frame-diff-test PRIVATE
  ${CMAKE_SOURCE_DIR}/../third_party/stb)
add_test(NAME frame-diff-identical
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/frame_diff
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_frames/frame_a.png
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_frames/frame_a.png
          1
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../tools)
add_test(NAME frame-diff-different
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/frame_diff
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_frames/frame_a.png
          ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_frames/frame_b.png
          0
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../tools)
set_property(TEST frame-diff-different PROPERTY WILL_FAIL TRUE)
# GSP boot / fill / palette routine transcription (ROM 0x00034A-0x0007D4) with
# a fake TMS34010 host port that records every (GSP address, word) pair. The
# transport seam is a callback v-table in gsp_boot.h; wiring it to musashi_cpu.c
# is deliberately not part of this slice. Mirrors the other flat C slice tests.
add_executable(stunrun-gsp-boot-slice-test
  gsp_boot.c
  gsp_boot_test.c)
set_target_properties(stunrun-gsp-boot-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-boot-slice-c COMMAND stunrun-gsp-boot-slice-test)

# --- gsp_boot_entry slice --------------------------------------------------
# MC68010 boot entry (ROM $0000506C-$00005078): sets SP to $FFFFC000,
# materialises the first continuation address ($507A) and tail-branches into the
# GSP boot at $53F2.  The routine has no jsr xref -- only three jmp entries off
# the $0100 thunk table -- so it must never be modelled as a C function that
# returns.  See docs/mc68010-boot-findings.md section 32.
add_executable(stunrun-gsp-boot-entry-slice-test
  gsp_boot_entry.c
  gsp_boot_entry_test.c)
set_target_properties(stunrun-gsp-boot-entry-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-boot-entry-slice-c
  COMMAND stunrun-gsp-boot-entry-slice-test)

# --- gsp_zero_fill slice ---------------------------------------------------
# MC68010 boot VRAM fill (ROM $0000548E-$000054C0): 256 x 900 = 230400 HSTDAT
# writes reached through $C0000C, which the pinned driver's host-port decode
# discards.  The test asserts the full ordered bus stream, the watchdog cadence
# and -- the load-bearing zero -- that no data write reaches a live register,
# so the routine is observably a no-op on memory.  See docs/mc68010-boot-findings.md
# sections 31.2 and 31.4 and native/gsp_zero_fill.h.
add_executable(stunrun-gsp-zero-fill-slice-test
  gsp_zero_fill.c
  gsp_zero_fill_test.c)
set_target_properties(stunrun-gsp-zero-fill-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-zero-fill-slice-c
  COMMAND stunrun-gsp-zero-fill-slice-test)

# MC68010 diagnostic/GSP handshake transcription (ROM 0x004F82-0x00506A).
# The test asserts the host-address sequence, marker initialization and the
# 16-word diagnostic copy; the opaque subroutines remain callback seams.
add_executable(stunrun-gsp-diagnostic-poll-slice-test
  gsp_diagnostic_poll.c
  gsp_diagnostic_poll_test.c)
set_target_properties(stunrun-gsp-diagnostic-poll-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-diagnostic-poll-slice-c
  COMMAND stunrun-gsp-diagnostic-poll-slice-test)

# --- ram_march slice -------------------------------------------------------
# STUN Runner's factory RAM test, transcribed from MC68010 ROM at 0x59D8-0x5EB8
# into native/ram_march.c.  The ROM reports pass/fail to the GSP FIFO as
# $3F3F / $5F5F / $9F9F / $FFFF, so the test asserts against the machine's own
# oracle rather than an invented one.  See native/ram_march.h.
add_executable(stunrun-ram-march-slice-test
  ram_march.c
  ram_march_test.c)
set_target_properties(stunrun-ram-march-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME ram-march-slice-c COMMAND stunrun-ram-march-slice-test)

# --- gsp_display_list slice -----------------------------------------------
# GSP display-list interpreter (ROM 0x000225B0-0x00022748), the sparse fill
# whose middle is the 0xBF4 nest (0x00000BC4-0x00000C26), the base-address
# helper 0x2418 and the $20-stride fill 0x00000C46.  The test asserts the exact
# ordered FIFO word sequence, the exact ordered address-latch sequence and the
# exact stream length for each stream; the expectations are written from the
# disassembly, not captured from the transcription.  Transport seam is the
# callback v-table in gsp_display_list.h, same field order as stunrun_gsp_host_t
# plus a byte-wide poke for 0x225B0's `move.b D0,(-$20,A1)`.  Mirrors the
# gsp_boot slice.  The separate integration test below wires this seam to the
# canonical Musashi host-port and work-RAM model.
add_executable(stunrun-gsp-display-list-slice-test
  gsp_display_list.cpp
  gsp_display_list_test.c)
set_target_properties(stunrun-gsp-display-list-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-display-list-slice-c COMMAND stunrun-gsp-display-list-slice-test)

# Caller-side orchestration, chunk wrapper and pending-byte state machine.
# Deliberately not installed as live CPU overrides: callee word parity and
# MAME validation of trailing state writes are separate acceptance boundaries.
add_executable(stunrun-gsp-upload-callers-slice-test
  gsp_upload_callers.cpp
  gsp_upload_callers_test.c)
set_target_properties(stunrun-gsp-upload-callers-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-upload-callers-slice-c COMMAND stunrun-gsp-upload-callers-slice-test)

add_executable(stunrun-gsp-display-list-musashi-test
  gsp_display_list.cpp
  jsa_transport.c
  ../reproduction/sound/jsa_latch.c
  gsp_display_list_musashi_test.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c)
target_include_directories(stunrun-gsp-display-list-musashi-test PRIVATE
  . ../reproduction/sound
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi)
target_link_libraries(stunrun-gsp-display-list-musashi-test PRIVATE
  m
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-gsp-display-list-musashi-test PROPERTIES
  C_STANDARD 99)
add_test(NAME gsp-display-list-musashi-c
  COMMAND stunrun-gsp-display-list-musashi-test)

add_executable(stunrun-gsp-upload-callers-musashi-test
  gsp_upload_callers.cpp
  jsa_transport.c
  ../reproduction/sound/jsa_latch.c
  gsp_upload_callers_musashi_test.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c)
target_include_directories(stunrun-gsp-upload-callers-musashi-test PRIVATE
  . ../reproduction/sound
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi)
target_link_libraries(stunrun-gsp-upload-callers-musashi-test PRIVATE
  m
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-gsp-upload-callers-musashi-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-upload-callers-musashi-c
  COMMAND stunrun-gsp-upload-callers-musashi-test)
set_tests_properties(gsp-upload-callers-musashi-c PROPERTIES TIMEOUT 120)

# Observe ordered MC68010 writes in the TMS34010 host window and the board's
# /GSPRES control.  The fixture runs real Musashi MOVE.B/.W/.L instructions
# from synthetic RAM and checks raw addresses, lane merge, longword splitting,
# effective HSTDAT effects, and active-low reset assert/release sequencing.
add_executable(stunrun-gsp-host-write-observer-musashi-test
  gsp_host_write_observer_musashi_test.c
  jsa_transport.c
  ../reproduction/sound/jsa_latch.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c)
target_include_directories(stunrun-gsp-host-write-observer-musashi-test PRIVATE
  . ../reproduction/sound ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi)
target_link_libraries(stunrun-gsp-host-write-observer-musashi-test PRIVATE
  m
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-gsp-host-write-observer-musashi-test PROPERTIES
  C_STANDARD 99)
add_test(NAME gsp-host-write-observer-musashi-c
  COMMAND stunrun-gsp-host-write-observer-musashi-test)
set_tests_properties(gsp-host-write-observer-musashi-c PROPERTIES TIMEOUT 120)

# --- jsa_transport slice --------------------------------------------------
# The 68010 -> JSA-II sound-board transport.  harddriv.cpp:4806 maps all of
# 0x600000-0x603FFF to one 8-bit handler pair (main_command_w on write,
# main_response_r on read) with no offset decode; both sides of REGION_SOUND
# in musashi_cpu.c were inert stubs, so every `move.b d0,$600000.l` was
# dropped and nothing ever reached sound_dispatch_frame().  This test drives
# the real Musashi decoder with synthetic move.b/.w/.l instructions in main
# RAM and asserts the latch byte, the byte lane, the window edges, that a
# read leaves command_pending alone, and end-to-end that command $38 lands as
# PCM at a stub audio backend at 9039 Hz.  No ROMs required; point
# STUNRUN_JSA_TRANSPORT_ROMDIR at the ROM set to additionally assert that the
# encoding used is byte-for-byte the one in the real 68010 ROM at 0x023EF6.
add_executable(stunrun-jsa-transport-musashi-test
  jsa_transport.c
  jsa_transport_musashi_test.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  ../reproduction/sound/jsa_latch.c
  ../reproduction/sound/oki6295.c
  ../reproduction/sound/oki_cmdmap.c
  ../reproduction/sound/ym2151.c
  ../reproduction/sound/ymfm_chip.cpp
  ../reproduction/sound/sound_dispatch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c
  ${YMFM_OPM_SOURCES})
target_include_directories(stunrun-jsa-transport-musashi-test PRIVATE
  . ../reproduction/sound ../backend
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi
  ${YMFM_INC})
target_link_libraries(stunrun-jsa-transport-musashi-test PRIVATE
  m
  stunrun_gsp_board
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-jsa-transport-musashi-test PROPERTIES
  C_STANDARD 99 CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
add_test(NAME jsa-transport-musashi-c
  COMMAND stunrun-jsa-transport-musashi-test)
set_tests_properties(jsa-transport-musashi-c PROPERTIES TIMEOUT 120)

# --- gsp_display slice ----------------------------------------------------
# Scanline generation + palette decode for the STUN Runner TMS34010 display
# (MAME harddriv_v.cpp scanline_multisync() and gsp_palette_change()).  The
# synthetic checks pin the row/column derivation and the four-lines-per-row
# interleave without any captured data; the acceptance check, enabled with
# STUNRUN_GSP_DISPLAY_DUMPDIR, must reproduce MAME's own frame-600 output
# (47 distinct colours, RGB(177,46,36) dominant at 21.71%).  This module
# reads pixels that are already in VRAM -- drawing them is the TMS34010 core,
# which is not part of it.
add_executable(stunrun-gsp-display-slice-test
  gsp_display.c
  gsp_display_test.c
  render.c)
set_target_properties(stunrun-gsp-display-slice-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-display-slice-c COMMAND stunrun-gsp-display-slice-test)

# --- vendored TMS34010 SystemVerilog GSP core ------------------------------
# third_party/tms34010_sv is a pinned, byte-verified copy of
# birdybro/TMS34010_sv @ 94a258e (MIT).  This test proves the vendored tree is
# unmodified and still compiles; it does NOT prove the GSP integration works.
# It always runs: without Verilator the manifest and licence checks still
# apply and only the lint is skipped, so the ROM-free build never breaks.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME gsp-rtl-vendored
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-gsp-rtl)
  set_tests_properties(gsp-rtl-vendored PROPERTIES TIMEOUT 300)
endif()

# The Verilated core itself is OPTIONAL.  It is a provider, not a dependency.
# gsp_rtl/ is still configured without Verilator: the board model is plain
# C99 with no simulator in its dependency chain, so its test always runs and
# always guards the map.  Only the Verilated core and its harness are skipped.
# This matters because the public ROM-free build must not acquire a
# heavyweight simulator dependency.
add_subdirectory(gsp_rtl)

# MC68010 text-draw commentary log (STUNRUN_TEXT_LOG). Registers the test the
# subagent could not add itself; the block is reproduced in
# docs/mc68010-boot-findings.md section 23.7.
add_executable(stunrun-musashi-text-log-test
  musashi_text_log_test.c
  fake_ports.c
  musashi_cpu.c
  musashi_loader.c
  cpu_override.c
  jsa_transport.c
  ../reproduction/sound/jsa_latch.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kops.c)
target_include_directories(stunrun-musashi-text-log-test PRIVATE
  . ../reproduction/sound
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi)
target_link_libraries(stunrun-musashi-text-log-test PRIVATE
  m
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kcpu.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/m68kdasm.o
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/musashi/softfloat/softfloat.o)
set_target_properties(stunrun-musashi-text-log-test PROPERTIES C_STANDARD 99)
add_test(NAME musashi-text-log-c COMMAND stunrun-musashi-text-log-test)

# GSP RTL driver (C++20, links the Verilated core).
# Only available when Verilator was found (same check as gsp_rtl subdirectory).
# The shell links against it only when STUNRUN_GSP_RTL=1, guarded by the same
# find_program result so the ROM-free build never requires Verilator.
find_program(STUNRUN_VERILATOR NAMES verilator
  HINTS
    "$ENV{HOME}/.local/share/verilator-5.052/bin"
    "$ENV{HOME}/opt/verilator/bin"
    "$ENV{HOME}/.local/bin")
if(STUNRUN_VERILATOR)
  add_library(stunrun_gsp_rtl_driver STATIC
    gsp_rtl_driver.cpp)
  target_include_directories(stunrun_gsp_rtl_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tms34010_sv)
  target_link_libraries(stunrun_gsp_rtl_driver PUBLIC
    stunrun_tms34010_sv
    stunrun_gsp_board
    ${VERILATOR_LIBS})
  set_target_properties(stunrun_gsp_rtl_driver PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()

# NOTE: this block must stay BELOW the driver's add_library above. An
# `if(TARGET ...)` placed earlier is always false here, because CMake has not
# reached the definition yet -- which fails silently into the fallback path
# and looks like "Verilator not found".
#
# STUNRUN_GSP_RTL=1 links the Verilated TMS34010 core into the shell so the
# live scanout reads what the GSP produced rather than what the 68010 uploaded.
#
# It is OFF by default and guarded twice over: the option must be set AND
# Verilator must have been found. The ROM-free build therefore gains no
# simulator dependency, which is the property the M0 baseline established.
#
# See docs/mc68010-boot-findings.md section 38 for what this does and does not
# make true: the core is byte-accurate against MAME on its first output, but it
# runs on its own clock here, so the picture is the GSP's work at an unrelated
# point in the game's timeline.
if(STUNRUN_GSP_RTL)
  if(TARGET stunrun_gsp_rtl_driver)
    target_compile_definitions(stunrun-native-shell PRIVATE STUNRUN_GSP_RTL=1)
    target_link_libraries(stunrun-native-shell PRIVATE stunrun_gsp_rtl_driver)
    add_executable(stunrun-gsp-replay
      gsp_replay.cpp gsp_display.c render.c)
    target_include_directories(stunrun-gsp-replay PRIVATE
      . .. ../backend gsp_rtl)
    target_link_libraries(stunrun-gsp-replay PRIVATE
      stunrun_backends stunrun_gsp_rtl_driver stunrun_gsp_board)
    set_target_properties(stunrun-gsp-replay PROPERTIES
      C_STANDARD 99 CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    message(STATUS "GSP RTL core linked into the shell (STUNRUN_GSP_RTL=1)")
  else()
    message(WARNING
      "STUNRUN_GSP_RTL=1 was requested but the driver target is not available, "
      "so the shell will fall back to the native software GSP path and no "
      "TMS34010 core will run. The usual cause is Verilator not being found "
      "(see the STUNRUN_VERILATOR HINTS below); it can ALSO mean this block "
      "has been moved ABOVE the driver's add_library, where the target does "
      "not exist yet -- the message used to blame Verilator for that, which "
      "sent the last person looking in the wrong place entirely.")
  endif()
endif()
