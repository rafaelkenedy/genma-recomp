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
#   release:     XBE_RELEASE=ON with the LLVM-MinGW toolchain, no game files needed: the runtime library, the launcher
#                and package_GenmaOnimusha (release/GenmaOnimusha/), which builds the game on the player's PC
option(XBE_RELEASE "build the release without game code (needs cmake/llvm-mingw.cmake)" OFF)
set(XBE_RELEASE_TITLE "Genma Onimusha")
set(XBE_UPDATE_REPOSITORY "rafaelkenedy/genma-recomp")
if(XBE_RELEASE)
    add_xbe_release(GenmaOnimusha default.xbe game.toml game)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xbe)
    # target name = GAME_EXE in build.bat, xbe = GAME_XBE
    add_xbe_image(GenmaOnimusha default.xbe game.toml ${GAME_PARTITIONS} game)
else()
    message(STATUS "assets/default.xbe is missing: only the tools are built (or configure with -DXBE_RELEASE=ON)")
endif()

