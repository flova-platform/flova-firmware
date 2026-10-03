add_library(flova-linux STATIC src/FlovaLinux.cpp)
add_library(Flova::Linux ALIAS flova-linux)
set_target_properties(flova-linux PROPERTIES EXPORT_NAME Linux)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
target_compile_features(flova-linux PUBLIC cxx_std_11)
target_include_directories(flova-linux
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/flova-linux>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/packages/flova-embedded-sdk/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(flova-linux PUBLIC flova-core)
target_link_libraries(flova-linux PRIVATE OpenSSL::Crypto)
target_compile_options(flova-linux PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

include(GNUInstallDirs)
install(TARGETS flova-linux EXPORT FlovaLinuxTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
  include/FlovaLinux.h
  include/FlovaLinuxEthernet.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/flova-linux)
install(EXPORT FlovaLinuxTargets
  FILE FlovaLinuxTargets.cmake
  NAMESPACE Flova::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlovaLinux)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/FlovaLinuxConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/FlovaLinuxConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlovaLinux)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/FlovaLinuxConfigVersion.cmake
  VERSION 0.1.0
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/FlovaLinuxConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/FlovaLinuxConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlovaLinux)
