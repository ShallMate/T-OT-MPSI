include_guard(GLOBAL)

include(FetchContent)
find_package(Git REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED COMPONENTS CXX)
find_package(OpenSSL REQUIRED)

# These revisions are intentionally pinned. They match the APIs used by the
# protocol sources and make a clean build reproducible.
set(LIBOTE_STD_VER 20 CACHE STRING "" FORCE)
set(CRYPTO_TOOLS_STD_VER 20 CACHE STRING "" FORCE)
set(ENABLE_COPROTO ON CACHE BOOL "" FORCE)
set(ENABLE_CIRCUITS ON CACHE BOOL "" FORCE)
set(ENABLE_SODIUM ON CACHE BOOL "" FORCE)
set(ENABLE_RELIC OFF CACHE BOOL "" FORCE)
set(ENABLE_SIMPLESTOT ON CACHE BOOL "" FORCE)
set(ENABLE_SIMPLESTOT_ASM ON CACHE BOOL "" FORCE)
set(ENABLE_IKNP ON CACHE BOOL "" FORCE)
set(ENABLE_KOS ON CACHE BOOL "" FORCE)
set(ENABLE_SOFTSPOKEN_OT ON CACHE BOOL "" FORCE)
set(ENABLE_SILENTOT ON CACHE BOOL "" FORCE)
set(ENABLE_SILENT_VOLE ON CACHE BOOL "" FORCE)
set(ENABLE_PPRF ON CACHE BOOL "" FORCE)
set(ENABLE_BITPOLYMUL ON CACHE BOOL "" FORCE)
set(ENABLE_LOGVOLE OFF CACHE BOOL "" FORCE)
set(ENABLE_BOOST ON CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SSE ON CACHE BOOL "" FORCE)
set(ENABLE_AVX ON CACHE BOOL "" FORCE)
set(ENABLE_AVX2 ON CACHE BOOL "" FORCE)
set(ENABLE_AVX512 ${OTMPSI_NATIVE_OPTIMIZATIONS} CACHE BOOL "" FORCE)
set(ENABLE_BMI2 ${OTMPSI_NATIVE_OPTIMIZATIONS} CACHE BOOL "" FORCE)
set(FETCH_AUTO ON CACHE BOOL "" FORCE)
set(FETCH_COPROTO ON CACHE BOOL "" FORCE)
set(FETCH_LIBDIVIDE ON CACHE BOOL "" FORCE)
set(FETCH_SODIUM ON CACHE BOOL "" FORCE)
set(FETCH_BITPOLYMUL ON CACHE BOOL "" FORCE)
set(NO_KOS_WARNING ON CACHE BOOL "" FORCE)
set(NO_SYSTEM_PATH ON CACHE BOOL "" FORCE)
set(MACORO_TESTS OFF CACHE BOOL "" FORCE)

set(LIBOTE_BUILD_DIR
    "${CMAKE_BINARY_DIR}/_deps/libote-build" CACHE PATH "" FORCE)
set(CRYPTOTOOLS_BUILD_DIR
    "${CMAKE_BINARY_DIR}/_deps/libote-build/cryptoTools" CACHE PATH "" FORCE)
set(OC_THIRDPARTY_CLONE_DIR
    "${CMAKE_BINARY_DIR}/_deps/libote-thirdparty/src" CACHE PATH "" FORCE)
set(OC_THIRDPARTY_INSTALL_PREFIX
    "${CMAKE_BINARY_DIR}/_deps/libote-thirdparty/install" CACHE PATH "" FORCE)
set(OC_THIRDPARTY_HINT
    "${CMAKE_BINARY_DIR}/_deps/libote-thirdparty/install" CACHE PATH "" FORCE)
file(MAKE_DIRECTORY "${LIBOTE_BUILD_DIR}" "${CRYPTOTOOLS_BUILD_DIR}")

if(NOT TARGET libdivide)
  add_library(libdivide INTERFACE IMPORTED GLOBAL)
  set_target_properties(
    libdivide PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OC_THIRDPARTY_INSTALL_PREFIX}/include"
  )
endif()

if(NOT TARGET libOTe)
  FetchContent_Declare(
    libote
    GIT_REPOSITORY https://github.com/osu-crypto/libOTe.git
    GIT_TAG 0412d3114bcf9c9955d0647aad8e49fe6a444461
    GIT_SHALLOW FALSE
    GIT_SUBMODULES_RECURSE TRUE
    PATCH_COMMAND
      "${CMAKE_COMMAND}"
        -DPATCH_SOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/libote-fetch-mkdir.patch
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyGnuPatchIfNeeded.cmake"
      COMMAND
      "${CMAKE_COMMAND}" -DLIBOTE_SOURCE_DIR=<SOURCE_DIR>
        -P "${CMAKE_CURRENT_LIST_DIR}/PatchLibOte.cmake"
  )
  FetchContent_MakeAvailable(libote)
endif()

if(NOT TARGET oc::libOTe)
  add_library(oc::libOTe ALIAS libOTe)
endif()

set(SECUREJOIN_CPP_VER 20)
set(SECUREJOIN_ENABLE_ASAN OFF)
set(SECUREJOIN_ENABLE_PAILLIER OFF)
set(SECUREJOIN_ENABLE_FAKE_GEN OFF)
set(SEC_JOIN_ROOT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(NOT TARGET secureJoin)
  FetchContent_Declare(
    secure_join_source
    GIT_REPOSITORY https://github.com/ladnir/secure-join.git
    GIT_TAG 377ca63b9d8f4f6aede0d3a2e3d9078973a3ee10
    GIT_SHALLOW FALSE
    PATCH_COMMAND
      "${CMAKE_COMMAND}"
        -DPATCH_SOURCE_DIR=<SOURCE_DIR>
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/secure-join-libote-compat.patch
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyGnuPatchIfNeeded.cmake"
  )
  FetchContent_GetProperties(secure_join_source)
  if(NOT secure_join_source_POPULATED)
    FetchContent_Populate(secure_join_source)
    add_subdirectory(
      "${secure_join_source_SOURCE_DIR}/secure-join"
      "${secure_join_source_BINARY_DIR}/secure-join"
      EXCLUDE_FROM_ALL
    )
  endif()
endif()

target_compile_features(secureJoin PUBLIC cxx_std_20)
target_compile_definitions(secureJoin PRIVATE mMultType=mLpnMultType)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    secureJoin
    PRIVATE
      -fpermissive -maes -mpclmul -msse4.1 -mavx -mavx2
      -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
  )
  if(OTMPSI_NATIVE_OPTIMIZATIONS)
    target_compile_options(secureJoin PRIVATE -mbmi2)
  endif()
endif()
