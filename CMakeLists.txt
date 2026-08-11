cmake_minimum_required(VERSION 3.10)
project(flova_firmware_core LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(flova-link-cbor STATIC
  third_party/zcbor/src/zcbor_common.c
  third_party/zcbor/src/zcbor_decode.c
  third_party/zcbor/src/zcbor_encode.c
  third_party/zcbor/src/zcbor_print.c
  protocol/generated/firmware/src/flova_link_decode.c
  protocol/generated/firmware/src/flova_link_encode.c)
target_include_directories(flova-link-cbor PUBLIC
  third_party/zcbor/include
  protocol/generated/firmware/include)
target_compile_definitions(flova-link-cbor PRIVATE ZCBOR_CANONICAL)

add_library(flova-core INTERFACE)
target_include_directories(flova-core INTERFACE
  packages/flova-device-sdk/include
  protocol/generated/firmware/include
  third_party/zcbor/include)
target_link_libraries(flova-core INTERFACE flova-link-cbor)
target_compile_definitions(flova-core INTERFACE
  FLOVA_DATASTREAM_CAPACITY=64 FLOVA_TEXT_CAPACITY=128 FLOVA_HISTORY_CAPACITY=64
  FLOVA_COMMAND_DEDUP_CAPACITY=16 FLOVA_HARDWARE_INPUT_CAPACITY=16
  FLOVA_HARDWARE_OUTPUT_CAPACITY=16 FLOVA_SCHEDULE_CAPACITY=16
  FLOVA_SCHEDULE_OCCURRENCE_CAPACITY=128)
add_executable(flova-core-test test/host/test_flova_core.cpp)
target_link_libraries(flova-core-test PRIVATE flova-core)
add_executable(flova-configuration-installer-test test/host/test_flova_configuration_installer.cpp)
target_link_libraries(flova-configuration-installer-test PRIVATE flova-core)
add_executable(flova-link-cbor-test test/host/test_flova_link_cbor.cpp)
target_link_libraries(flova-link-cbor-test PRIVATE flova-core)
add_executable(flova-link-fuzz-test test/host/test_flova_link_fuzz.cpp)
target_link_libraries(flova-link-fuzz-test PRIVATE flova-core)
add_executable(flova-ws-test test/host/test_flova_ws.cpp)
target_include_directories(flova-ws-test PRIVATE
  test/host/arduino-shim
  packages/flova-arduino/include)
add_executable(flova-custom-board examples/custom-board-basic/src/main.cpp)
target_link_libraries(flova-custom-board PRIVATE flova-core)
enable_testing()
add_test(NAME flova-core-test COMMAND flova-core-test)
add_test(NAME flova-configuration-installer-test COMMAND flova-configuration-installer-test)
add_test(NAME flova-link-cbor-test COMMAND flova-link-cbor-test)
add_test(NAME flova-link-fuzz-test COMMAND flova-link-fuzz-test)
add_test(NAME flova-ws-test COMMAND flova-ws-test)
