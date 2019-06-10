cmake_minimum_required(VERSION 3.5)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE release)
endif()

include(ExternalProject)
# if no cdt root is given use default path
if(EOSIO_CDT_ROOT STREQUAL "" OR NOT EOSIO_CDT_ROOT)
   find_package(eosio.cdt)
endif()

ExternalProject_Add(
   eosbancor
   SOURCE_DIR ${CMAKE_SOURCE_DIR}/src
   BINARY_DIR ${CMAKE_BINARY_DIR}/eosbancor
   CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${EOSIO_CDT_ROOT}/lib/cmake/eosio.cdt/EosioWasmToolchain.cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
   UPDATE_COMMAND ""
   PATCH_COMMAND ""
   TEST_COMMAND ""
   INSTALL_COMMAND ""
   BUILD_ALWAYS 1
)
