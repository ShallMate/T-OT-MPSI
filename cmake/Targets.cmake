include_guard(GLOBAL)

set(
  OTMPSI_SOURCES
  src/otmpsi/GMW/Circuit.cpp
  src/otmpsi/GMW/Gmw.cpp
  src/otmpsi/GMW/SilentTripleGen.cpp
  src/otmpsi/b2a_phase.cc
  src/otmpsi/band_okvs_codec.cc
  src/otmpsi/band_okvs_params.cc
  src/otmpsi/bandokvs/band.cc
  src/otmpsi/bandokvs/band_okvs.cc
  src/otmpsi/bandokvs/utils.cc
  src/otmpsi/compat/parallel.cc
  src/otmpsi/demo_data.cc
  src/otmpsi/final_reveal_phase.cc
  src/otmpsi/gmw_inputs.cc
  src/otmpsi/input_preparation.cc
  src/otmpsi/nparty_gmw.cc
  src/otmpsi/otmpsi_ports.cc
  src/otmpsi/otmpsi_runner.cc
  src/otmpsi/otmpsi_types.cc
  src/otmpsi/packed_chosen_ot.cc
  src/otmpsi/pairwise_zero_phase.cc
  src/otmpsi/private_selector_ot.cc
  src/otmpsi/public_t_compare.cc
  src/otmpsi/relaxed_selector_open.cc
  src/otmpsi/ssbsmt_phase.cc
  src/otmpsi/tree_gmw_correlation_pool.cc
  src/otmpsi/tree_x2a_eda_bit.cc
  src/otmpsi/z256_bit_mul_ot.cc
)

add_library(otmpsi STATIC ${OTMPSI_SOURCES})
add_library(otmpsi::otmpsi ALIAS otmpsi)
target_compile_features(otmpsi PUBLIC cxx_std_20)
target_include_directories(
  otmpsi
  PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_compile_definitions(
  otmpsi
  PUBLIC COPROTO_ENABLE_BOOST LIBDIVIDE_AVX2
)
target_link_libraries(
  otmpsi
  PUBLIC
    oc::libOTe
    secureJoin
    BLAKE3::blake3
    OpenMP::OpenMP_CXX
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    otmpsi
    PRIVATE
      -O3 -fcoroutines -fpermissive
      -Wall -Wextra
      -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable
      -maes -mpclmul -msse2 -msse3 -msse4.1 -mavx -mavx2
  )
  if(OTMPSI_NATIVE_OPTIMIZATIONS)
    target_compile_options(otmpsi PRIVATE -mbmi2 -mavx512f)
  endif()
  target_link_options(otmpsi INTERFACE -Wl,--allow-multiple-definition)
endif()

if(OTMPSI_BUILD_APPS)
  add_executable(otmpsi_demo apps/otmpsi_demo.cc)
  target_link_libraries(otmpsi_demo PRIVATE otmpsi::otmpsi)

  add_executable(gmw_triple_bench apps/gmw_triple_bench.cc)
  target_link_libraries(gmw_triple_bench PRIVATE otmpsi::otmpsi)
endif()
