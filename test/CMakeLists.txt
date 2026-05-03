add_executable(tests Tests.cpp)

target_link_libraries(
  tests
  PRIVATE
  DDB::LibDDB
  Catch2::Catch2WithMain
)

add_subdirectory(targets)
