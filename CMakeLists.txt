cmake_minimum_required(VERSION 3.10)
project(flova_firmware_core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(flova-core INTERFACE)
target_include_directories(flova-core INTERFACE packages/flova-device-sdk/include)
target_compile_definitions(flova-core INTERFACE
  FLOVA_DATASTREAM_CAPACITY=16 FLOVA_TEXT_CAPACITY=128 FLOVA_HISTORY_CAPACITY=64
  FLOVA_COMMAND_DEDUP_CAPACITY=16 FLOVA_HARDWARE_INPUT_CAPACITY=16
  FLOVA_HARDWARE_OUTPUT_CAPACITY=16 FLOVA_SCHEDULE_CAPACITY=16
  FLOVA_SCHEDULE_OCCURRENCE_CAPACITY=128)
add_executable(flova-core-test test/native/test_flova_core.cpp)
target_link_libraries(flova-core-test PRIVATE flova-core)
add_executable(flova-custom-board examples/custom-board-basic/src/main.cpp)
target_link_libraries(flova-custom-board PRIVATE flova-core)
enable_testing()
add_test(NAME flova-core-test COMMAND flova-core-test)
