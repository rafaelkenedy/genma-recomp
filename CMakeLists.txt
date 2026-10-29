# Genma Onimusha (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ in this repository (sdk/UPSTREAM.md), or two folders up inside the SDK's templates
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    message(FATAL_ERROR "sdk/ is missing: the port needs the SDK copy kept in this repository")
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# Two builds:
#   development: the game in assets/, the whole exe built here (build.bat, any clang toolchain)
#   release:     XBE_RELEASE=ON with the platform release toolchain, no game files needed: the runtime library, the launcher
#                and package_GenmaOnimusha (release/GenmaOnimusha/), which builds the game on the player's PC
option(XBE_RELEASE "Build the asset-free release with the platform's bundled toolchain" OFF)
set(XBE_RELEASE_TITLE "Genma Onimusha")
set(XBE_UPDATE_REPOSITORY "rafaelkenedy/genma-recomp")
if(NOT XBE_BUILD_RUNTIME)
    if(XBE_RELEASE)
        message(FATAL_ERROR "XBE_RELEASE requires XBE_BUILD_RUNTIME=ON")
    endif()
    message(STATUS "Building native tools only; no graphical game runtime or game assets are required")
elseif(XBE_RELEASE)
    add_xbe_release(GenmaOnimusha default.xbe game.toml game)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
    # target name = GAME_EXE in build.bat, xbe = GAME_XBE
    add_xbe_image(GenmaOnimusha default.xbe game.toml ${GAME_PARTITIONS} game)
else()
    message(STATUS "assets/default.xbe is missing: only the tools are built (or configure with -DXBE_RELEASE=ON)")
endif()

option(XBE_BUILD_TESTS "Build asset-free core and game-policy regression tests" OFF)
option(XBE_BUILD_CONTROLLER_BRIDGE "Build the Windows XInput helper for local WSL tests" OFF)
if(XBE_BUILD_CONTROLLER_BRIDGE)
    if(NOT WIN32)
        message(FATAL_ERROR "The controller bridge helper must be built with a Windows toolchain")
    endif()
    add_executable(genma_controller_bridge tools/controller_bridge_win.cpp)
    target_include_directories(genma_controller_bridge PRIVATE ${SDK}/runtime)
endif()
if(XBE_BUILD_TESTS)
    enable_testing()
    include(cmake/CoreTests.cmake)
endif()

if(XBE_BUILD_WINDOW_TEST)
    add_executable(genma_window_smoke tools/window_smoke.cpp tools/window_vulkan.cpp)
    target_link_libraries(genma_window_smoke PRIVATE SDL3::SDL3-static)
    target_compile_options(genma_window_smoke PRIVATE -Wall -Wextra)
    if(XBE_BUILD_TESTS)
        add_test(NAME test_sdl_window COMMAND genma_window_smoke --self-test)
        set_tests_properties(test_sdl_window PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 30)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND XBE_BUILD_TESTS AND (XBE_BUILD_RUNTIME OR XBE_BUILD_WINDOW_TEST))
    add_executable(test_wsl_controller_bridge tools/test_wsl_controller_bridge.cpp
        ${SDK}/runtime/core/wsl_controller_bridge.cpp ${SDK}/runtime/core/log.cpp)
    target_include_directories(test_wsl_controller_bridge PRIVATE ${SDK}/runtime)
    target_link_libraries(test_wsl_controller_bridge PRIVATE SDL3::SDL3-static)
    target_compile_options(test_wsl_controller_bridge PRIVATE -UNDEBUG -Wall -Wextra)
    add_test(NAME test_wsl_controller_bridge COMMAND test_wsl_controller_bridge)
    set_tests_properties(test_wsl_controller_bridge PROPERTIES TIMEOUT 15)
endif()

