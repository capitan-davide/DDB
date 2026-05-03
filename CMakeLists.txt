cmake_minimum_required(VERSION 4.0)
project(DDB VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    libedit
    REQUIRED IMPORTED_TARGET libedit
)

add_subdirectory(lib)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 CONFIG REQUIRED)
    add_subdirectory(test)
endif()
