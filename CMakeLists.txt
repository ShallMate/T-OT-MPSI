cmake_minimum_required(VERSION 3.20)

project(
  otmpsi
  VERSION 1.0.0
  DESCRIPTION "Standalone threshold OT-MPSI research implementation"
  LANGUAGES C CXX ASM
)

include(GNUInstallDirs)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(OTMPSI_TOP_LEVEL_DEFAULT ON)
else()
  set(OTMPSI_TOP_LEVEL_DEFAULT OFF)
endif()

option(OTMPSI_BUILD_APPS "Build the command-line demo and benchmark"
       ${OTMPSI_TOP_LEVEL_DEFAULT})
option(OTMPSI_BUILD_TESTS "Build focused protocol smoke tests"
       ${OTMPSI_TOP_LEVEL_DEFAULT})
option(OTMPSI_NATIVE_OPTIMIZATIONS
       "Enable the AVX-512/BMI2 flags used by the original YACL target" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Targets)

if(OTMPSI_BUILD_TESTS)
  include(CTest)
  add_executable(tree_x2a_selector_smoke tests/tree_x2a_selector_smoke.cc)
  target_link_libraries(tree_x2a_selector_smoke PRIVATE otmpsi::otmpsi)
  add_test(NAME tree_x2a_selector_smoke COMMAND tree_x2a_selector_smoke)

  add_executable(public_t_compare_smoke tests/public_t_compare_smoke.cc)
  target_link_libraries(public_t_compare_smoke PRIVATE otmpsi::otmpsi)
  add_test(NAME public_t_compare_smoke COMMAND public_t_compare_smoke)
endif()

install(
  TARGETS otmpsi
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
if(OTMPSI_BUILD_APPS)
  install(TARGETS otmpsi_demo gmw_triple_bench
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(
  DIRECTORY src/otmpsi
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h"
)
