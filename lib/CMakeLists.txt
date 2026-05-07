add_library(LibDDB
  Error.cpp
  Pipe.cpp
  Process.cpp
  Registers.cpp
)

add_library(DDB::LibDDB ALIAS LibDDB)

set_target_properties(
  LibDDB
  PROPERTIES OUTPUT_NAME DDB
)

target_compile_features(
  LibDDB
  PUBLIC cxx_std_17
)

target_include_directories(
  LibDDB
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/include
)

include(GNUInstallDirs)
install(
  TARGETS LibDDB
  EXPORT DDBTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  DIRECTORY ${PROJECT_SOURCE_DIR}/include
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT DDBTargets
  FILE DDBConfig.cmake
  NAMESPACE DDB::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DDB
)
