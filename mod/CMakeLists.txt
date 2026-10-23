# Archipelago mod for the Gizmos & Gadgets recompilation.
#   cmake -S mod -B build-mod -DGG_RECOMP_DIR=/path/to/gizmos-and-gadgets-recomp
#   cmake --build build-mod      -> ap_mod.dll / ap_mod.so, copy into <game dir>/mods/
cmake_minimum_required(VERSION 3.20)
project(gg_ap_mod C)
set(GG_RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../gizmos-and-gadgets-recomp" CACHE PATH "recomp checkout (for include/gg_mod.h)")
if(NOT EXISTS "${GG_RECOMP_DIR}/include/gg_mod.h")
  message(FATAL_ERROR "gg_mod.h not found: set GG_RECOMP_DIR to a gizmos-and-gadgets-recomp checkout")
endif()
add_library(ap_mod MODULE ap_mod.c picker.c)
set_target_properties(ap_mod PROPERTIES PREFIX "" C_STANDARD 99)
target_include_directories(ap_mod PRIVATE "${GG_RECOMP_DIR}/include")
if(NOT MSVC)
  target_compile_options(ap_mod PRIVATE -Wall -Wextra -O2)
endif()

option(GG_AP_TESTS "Build the headless test helper mod" OFF)
if(GG_AP_TESTS)
  add_library(test_driver MODULE tests/test_driver.c)
  set_target_properties(test_driver PROPERTIES PREFIX "")
  target_include_directories(test_driver PRIVATE "${GG_RECOMP_DIR}/include")
endif()
