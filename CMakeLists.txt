cmake_minimum_required(VERSION 3.25)
project(aliens-invaders
    VERSION 0.10.0
    DESCRIPTION "Arcade vertical space shooter powered by modern C++20 and SDL3"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_SANITIZERS "Compile with AddressSanitizer and UBSan" OFF)
option(BUILD_TESTS "Build headless C++20 automated test suite" ON)

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -pedantic -Wshadow -Wcast-align
        -Woverloaded-virtual -Wnon-virtual-dtor -Wformat=2
        -Wformat-security -Wconversion -Wsign-conversion -Wnull-dereference
    )
    if(ENABLE_SANITIZERS)
        add_compile_options(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        add_compile_options(-O2 -fomit-frame-pointer -fstack-protector-strong)
        add_compile_definitions(_FORTIFY_SOURCE=2 NDEBUG)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /O2)
    add_compile_definitions(NDEBUG)
endif()

add_executable(bake_assets tools/bake_assets.cc)
set_target_properties(bake_assets PROPERTIES CXX_STANDARD 20)

set(EMBEDDED_ASSETS_CC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/embedded_assets.cc")
file(GLOB PNG_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/png/*.png")

add_custom_command(
    OUTPUT "${EMBEDDED_ASSETS_CC}"
    COMMAND bake_assets
    DEPENDS bake_assets ${PNG_ASSETS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking PNG assets into C++20 embedded binary source..."
)

set(PROJECT_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entities"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/system"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
list(APPEND GAME_SOURCES "${EMBEDDED_ASSETS_CC}")

add_executable(aliens-invaders ${GAME_SOURCES})
target_include_directories(aliens-invaders PRIVATE ${PROJECT_INCLUDES})
target_compile_definitions(aliens-invaders PRIVATE VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(aliens-invaders PRIVATE SDL3::SDL3)
if(UNIX AND NOT APPLE)
    target_link_libraries(aliens-invaders PRIVATE m)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(unit_tests
        tests/unit_tests.cc
        src/system/highscore_table.cc
    )
    target_include_directories(unit_tests PRIVATE ${PROJECT_INCLUDES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(unit_tests PRIVATE m)
    endif()
    add_test(NAME CoreUnitTests COMMAND unit_tests)
endif()

include(GNUInstallDirs)
install(TARGETS aliens-invaders DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/desktop/aliens-invaders.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/desktop/aliens-invaders.6x DESTINATION ${CMAKE_INSTALL_MANDIR}/man6 RENAME aliens-invaders.6)
install(FILES assets/icons/hicolor/96x96/apps/aliens-invaders.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)

foreach(SIZE 48x48 96x96 128x128 256x256)
    install(FILES assets/icons/hicolor/${SIZE}/apps/aliens-invaders.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${SIZE}/apps)
endforeach()
