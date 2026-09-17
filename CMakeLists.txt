cmake_minimum_required(VERSION 3.10)
project(flova_firmware_core LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(flova-link-cbor STATIC
  third_party/zcbor/flova/zcbor_common.c
  third_party/zcbor/flova/zcbor_decode.c
  third_party/zcbor/flova/zcbor_encode.c
  third_party/zcbor/flova/zcbor_print.c
  protocol/generated/firmware/src/flova_link_decode.c
  protocol/generated/firmware/src/flova_link_encode.c)
target_include_directories(flova-link-cbor PUBLIC
  third_party/zcbor/include
  protocol/generated/firmware/include)

add_library(flova-core INTERFACE)
target_include_directories(flova-core INTERFACE
  packages/flova-embedded-sdk/include
  protocol/generated/firmware/include
  third_party/zcbor/include)
target_link_libraries(flova-core INTERFACE flova-link-cbor)
target_compile_options(flova-core INTERFACE
  -Wall -Wextra -fno-exceptions -fno-rtti)
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
target_link_libraries(flova-ws-test PRIVATE flova-core)
add_executable(flova-custom-board examples/custom-board-basic/src/main.cpp)
target_link_libraries(flova-custom-board PRIVATE flova-core)
add_executable(flova-configuration-test test/host/test_flova_configuration.cpp)
target_include_directories(flova-configuration-test PRIVATE
  packages/flova-arduino/include
  packages/flova-esp32/include)
target_link_libraries(flova-configuration-test PRIVATE flova-core)
add_executable(flova-runtime-services-test test/host/test_flova_runtime_services.cpp)
target_include_directories(flova-runtime-services-test PRIVATE packages/flova-arduino/include)
target_link_libraries(flova-runtime-services-test PRIVATE flova-core)
add_executable(flova-lifecycle-test test/host/test_flova_lifecycle.cpp)
target_include_directories(flova-lifecycle-test PRIVATE
  test/host/arduino-shim packages/flova-arduino/include)
target_link_libraries(flova-lifecycle-test PRIVATE flova-core)
enable_testing()
add_executable(flova-hardware-test test/host/test_flova_hardware.cpp)
target_include_directories(flova-hardware-test PRIVATE
  test/host/arduino-shim packages/flova-arduino/include
  packages/flova-esp32/include packages/flova-esp8266/include)
target_link_libraries(flova-hardware-test PRIVATE flova-core)
add_test(NAME flova-hardware-test COMMAND flova-hardware-test)
add_test(NAME flova-core-test COMMAND flova-core-test)
add_test(NAME flova-configuration-installer-test COMMAND flova-configuration-installer-test)
add_test(NAME flova-link-cbor-test COMMAND flova-link-cbor-test)
add_test(NAME flova-link-fuzz-test COMMAND flova-link-fuzz-test)
add_test(NAME flova-ws-test COMMAND flova-ws-test)
add_test(NAME flova-configuration-test COMMAND flova-configuration-test)
add_test(NAME flova-runtime-services-test COMMAND flova-runtime-services-test)
add_test(NAME flova-lifecycle-test COMMAND flova-lifecycle-test)

add_executable(flova-tls-memory-test test/host/test_flova_tls_memory.cpp)
target_include_directories(flova-tls-memory-test PRIVATE packages/flova-esp8266/include)
add_test(NAME flova-tls-memory-test COMMAND flova-tls-memory-test)

add_executable(flova-compact-runtime-test test/host/test_flova_compact_runtime.cpp)
target_link_libraries(flova-compact-runtime-test PRIVATE flova-core)
target_compile_definitions(flova-compact-runtime-test PRIVATE FLOVA_STREAM_DESCRIPTORS_IN_STORAGE=1)
add_test(NAME flova-compact-runtime-test COMMAND flova-compact-runtime-test)

function(add_flova_logging_test target)
  add_executable(${target} test/host/test_flova_logging.cpp)
  target_include_directories(${target} PRIVATE
    packages/flova-embedded-sdk/include packages/flova-arduino/include)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_flova_logging_test(flova-logging-default-test)
add_flova_logging_test(flova-logging-all-test)
target_compile_definitions(flova-logging-all-test PRIVATE FLOVA_LOG_LEVEL=FLOVA_LOG_LEVEL_TRACE)
add_flova_logging_test(flova-logging-disabled-test)
target_compile_definitions(flova-logging-disabled-test PRIVATE FLOVA_LOGGING_ENABLED=0)
