if(NOT DEFINED LIBOTE_SOURCE_DIR)
  message(FATAL_ERROR "LIBOTE_SOURCE_DIR is required")
endif()

set(libote_cmake "${LIBOTE_SOURCE_DIR}/CMakeLists.txt")
file(READ "${libote_cmake}" libote_text)
string(REPLACE
  "add_subdirectory(libOTe_Tests)"
  "add_subdirectory(libOTe_Tests EXCLUDE_FROM_ALL)"
  libote_text "${libote_text}"
)
string(REPLACE
  "add_subdirectory(frontend)"
  "add_subdirectory(frontend EXCLUDE_FROM_ALL)"
  libote_text "${libote_text}"
)
file(WRITE "${libote_cmake}" "${libote_text}")

set(cryptotools_cmake "${LIBOTE_SOURCE_DIR}/cryptoTools/CMakeLists.txt")
file(READ "${cryptotools_cmake}" cryptotools_text)
string(REPLACE
  "add_subdirectory(tests_cryptoTools)"
  "add_subdirectory(tests_cryptoTools EXCLUDE_FROM_ALL)"
  cryptotools_text "${cryptotools_text}"
)
string(REPLACE
  "add_subdirectory(frontend_cryptoTools)"
  "add_subdirectory(frontend_cryptoTools EXCLUDE_FROM_ALL)"
  cryptotools_text "${cryptotools_text}"
)
file(WRITE "${cryptotools_cmake}" "${cryptotools_text}")
