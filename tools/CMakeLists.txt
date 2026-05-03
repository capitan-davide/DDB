add_executable(ddb DDB.cpp)

target_link_libraries(
  ddb
  PRIVATE DDB::LibDDB PkgConfig::libedit
)

include(GNUInstallDirs)
install(
  TARGETS ddb
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
