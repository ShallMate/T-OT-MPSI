cmake_minimum_required(VERSION 3.20)
project(otmpsi_consumer LANGUAGES C CXX ASM)

add_subdirectory(../.. otmpsi-build)

add_executable(otmpsi_consumer main.cc)
target_link_libraries(otmpsi_consumer PRIVATE otmpsi::otmpsi)
