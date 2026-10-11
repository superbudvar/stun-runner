# GSP RTL provider.
#
# Two layers, deliberately separated:
#
#   gsp_board.c   plain C99 board model (memory map + host port).  No
#                 simulator, no ROM, no toolkit.  ALWAYS built and ALWAYS
#                 tested, so the map is guarded even on a machine that cannot
#                 run the core.
#
#   gsp_rtl_main.cpp  the Verilated harness.  OPTIONAL: configured only when
#                 Verilator is found, because Verilator is a heavy
#                 dependency and this project must keep building publicly
#                 without it.
#
# The core is a CPU core, in the same category as the Musashi 68010 already
# linked into this target.  It holds no game state, decides no policy, and is
# not reachable from any render loop: it sits below the toolkit-free seam.

# ---- always available: the board model ----------------------------------

add_library(stunrun_gsp_board STATIC gsp_board.c)
target_include_directories(stunrun_gsp_board PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(stunrun_gsp_board PROPERTIES C_STANDARD 99)

add_executable(stunrun-gsp-board-test gsp_board_test.c)
target_link_libraries(stunrun-gsp-board-test PRIVATE stunrun_gsp_board)
set_target_properties(stunrun-gsp-board-test PROPERTIES C_STANDARD 99)
add_test(NAME gsp-board-map-c COMMAND stunrun-gsp-board-test)

# ---- optional: the Verilated core ---------------------------------------

find_program(STUNRUN_VERILATOR NAMES verilator
  # Verilator has no system package here: the 5.052 ebuild is broken
  # (sci-electronics/verilator-5.052-r1::guru fails because PYTHON_COMPAT
  # includes unsupported python3_15) and 5.050 is ~amd64-masked.  A local
  # build therefore lives outside the system, so look in the conventional
  # per-user prefix before giving up.  This keeps the GSP provider
  # discoverable without every caller having to edit PATH.
  HINTS
    "$ENV{HOME}/.local/share/verilator-5.052/bin"
    "$ENV{HOME}/opt/verilator/bin"
    "$ENV{HOME}/.local/bin")
if(NOT STUNRUN_VERILATOR)
  message(STATUS
    "stunrun: verilator not found; the GSP RTL core and harness are not "
    "configured (the board model and its test still are)")
  return()
endif()

set(STUNRUN_TMS34010_SV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/tms34010_sv)
if(NOT EXISTS ${STUNRUN_TMS34010_SV_DIR}/rtl/tms34010_system.sv)
  message(FATAL_ERROR
    "stunrun: verilator found but ${STUNRUN_TMS34010_SV_DIR} is missing. "
    "Run tools/check-gsp-rtl for the vendored-tree contract.")
endif()

# Verilate the core into a C++ library.
#
# Use Verilator's own CMake package rather than feeding .sv files to
# add_library(): CMake has no SystemVerilog linker language for these sources,
# and the generated C++ must come from verilate() anyway.
#
# HINTS matters.  A locally built Verilator is not on CMAKE_PREFIX_PATH, and
# $ENV{VERILATOR_ROOT} is the documented way to point at it.  This is also the
# single place that has to change when Verilator moves out of /tmp.
get_filename_component(STUNRUN_VERILATOR_ROOT "${STUNRUN_VERILATOR}" DIRECTORY)
get_filename_component(STUNRUN_VERILATOR_ROOT "${STUNRUN_VERILATOR_ROOT}" DIRECTORY)
list(APPEND CMAKE_PREFIX_PATH
  "${STUNRUN_VERILATOR_ROOT}/share/verilator"
  "${STUNRUN_VERILATOR_ROOT}/../share/verilator")
find_package(verilator HINTS ${STUNRUN_VERILATOR_ROOT} QUIET)
if(NOT verilator_FOUND)
  message(STATUS
    "stunrun: found ${STUNRUN_VERILATOR} but not its CMake package; "
    "skipping the GSP RTL core")
  return()
endif()

# The package must be compiled first: every other module `import
# tms34010_pkg::*` and Verilator resolves it in command-line order, so a
# plain sorted glob fails with "Reference to 'local_word_t' before
# declaration".  This is the same ordering upstream's scripts/lint.sh uses.
set(STUNRUN_TMS34010_SV_SOURCES ${STUNRUN_TMS34010_SV_DIR}/rtl/tms34010_pkg.sv)
file(GLOB STUNRUN_TMS34010_SV_REST ${STUNRUN_TMS34010_SV_DIR}/rtl/*.sv
     ${STUNRUN_TMS34010_SV_DIR}/rtl/*/*.sv)
list(SORT STUNRUN_TMS34010_SV_REST)
foreach(f ${STUNRUN_TMS34010_SV_REST})
  if(NOT f STREQUAL ${STUNRUN_TMS34010_SV_DIR}/rtl/tms34010_pkg.sv)
    list(APPEND STUNRUN_TMS34010_SV_SOURCES ${f})
  endif()
endforeach()

# verilate() does not create its target; it populates one that must already
# exist, and it reads BINARY_DIR from it.  PREFIX is mandatory here because
# SOURCES is a list rather than a single top file.
add_library(stunrun_tms34010_sv STATIC)
verilate(stunrun_tms34010_sv
  PREFIX tms34010_sv
  SOURCES ${STUNRUN_TMS34010_SV_SOURCES}
  TOP_MODULE tms34010_system
  # Multiple tops are intentional upstream: the core, the video timing and the
  # refresh block are independently instantiable.  These relaxations match
  # what upstream's own scripts/lint.sh passes, and none of them change
  # behaviour -- they silence unused-parameter/style diagnostics only.
  VERILATOR_ARGS -Wno-fatal -Wno-MULTITOP -Wno-DECLFILENAME
                 -Wno-UNUSEDPARAM -Wno-UNUSEDSIGNAL)

add_executable(stunrun-gsp-rtl-harness gsp_rtl_main.cpp)
# ${CMAKE_CURRENT_SOURCE_DIR}/.. for musashi_cpu.h: the bus-event record layout
# must come from the producer's own header, never a hand-written copy.
target_include_directories(stunrun-gsp-rtl-harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(stunrun-gsp-rtl-harness PRIVATE stunrun_gsp_board stunrun_tms34010_sv)
set_target_properties(stunrun-gsp-rtl-harness PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
  C_STANDARD 99 C_STANDARD_REQUIRED ON)

# The ROM-less boot self test IS a unit test: it needs ~10 cycles to take the
# level-0 vector out of VRAM and reach the seeded entry, so it costs nothing.
# It asserts the PC actually arrives, not merely that the core did not crash.
add_test(NAME gsp-rtl-romless-boot
  COMMAND stunrun-gsp-rtl-harness --self-test --cycles 20000)
set_tests_properties(gsp-rtl-romless-boot PROPERTIES TIMEOUT 120)

# The long runs are deliberately NOT CTests: a 600-frame GSP run is 498M
# cycles, minutes of wall clock.  That is an integration check, not a unit
# test.  See stun-runner-gsvp.4.