cmake_minimum_required(VERSION 3.16)
project(CS3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2")
    add_compile_options(-Wall -Wextra)
endif()

# --- Find SDL2 ---------------------------------------------------------------
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

add_executable(cs3 ${SOURCES})

# On Windows, build a GUI app (no console window) and rely on SDL2main to
# provide the platform entry point that forwards to our main().
if(WIN32)
    set_target_properties(cs3 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(cs3 PRIVATE ${SDL2_INCLUDE_DIRS})

if(SDL2_LIBRARY_DIRS)
    target_link_directories(cs3 PRIVATE ${SDL2_LIBRARY_DIRS})
endif()

# Prefer modern imported targets when the SDL2 CMake config package is found,
# otherwise fall back to the variables provided by find_package / pkg-config.
if(TARGET SDL2::SDL2main)
    target_link_libraries(cs3 PRIVATE SDL2::SDL2main)
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(cs3 PRIVATE SDL2::SDL2)
else()
    target_link_libraries(cs3 PRIVATE ${SDL2_LIBRARIES})
endif()

# Copy SDL2.dll next to the executable on Windows so the game is runnable
# straight from the build/output directory.
if(WIN32)
    if(TARGET SDL2::SDL2)
        add_custom_command(TARGET cs3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:cs3>
            VERBATIM)
    endif()
endif()
