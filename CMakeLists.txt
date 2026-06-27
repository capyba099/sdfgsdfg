cmake_minimum_required(VERSION 3.16)
project(CS3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

# --- Find SDL2 ---------------------------------------------------------------
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(cs3 ${SOURCES})

target_include_directories(cs3 PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(cs3 PRIVATE ${SDL2_LIBRARIES})

if(SDL2_LIBRARY_DIRS)
    target_link_directories(cs3 PRIVATE ${SDL2_LIBRARY_DIRS})
endif()

# SDL2 may report the link target as "SDL2::SDL2" via config package.
if(TARGET SDL2::SDL2)
    target_link_libraries(cs3 PRIVATE SDL2::SDL2)
endif()
