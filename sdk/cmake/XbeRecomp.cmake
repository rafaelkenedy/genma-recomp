# Original Xbox static recompilation: xbe_recomp, the host runtime and add_xbe_image()
include_guard(GLOBAL)

get_filename_component(XBE_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
# build scripts find the SDK's xdk folder and THIRD_PARTY.txt through this
file(TO_NATIVE_PATH "${XBE_SDK_DIR}" XBE_SDK_DIR_NATIVE)
file(WRITE ${CMAKE_BINARY_DIR}/sdk_dir.txt "${XBE_SDK_DIR_NATIVE}\n")

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.0 GIT_SHALLOW TRUE)
# SDL3 for controllers, static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3)

# x86 to C++ recompiler
file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${XBE_RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE ${XBE_SDK_DIR}/runtime ${XBE_SDK_DIR}/tools/recomp)
target_compile_definitions(xbe_recomp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

# XDK signature scanner that writes a new title's config
add_executable(xbe_symbols ${XBE_SDK_DIR}/tools/symbols/main.cpp)
target_include_directories(xbe_symbols PRIVATE ${XBE_SDK_DIR}/runtime)
target_compile_definitions(xbe_symbols PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_symbols PRIVATE Zydis tomlplusplus::tomlplusplus)

set(XBE_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)

file(GLOB XBE_RUNTIME_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/*/*.cpp)
# SDL's headers go to the input code alone (it is linked LINK_ONLY), so the generated sources' compile line stays the same
set_source_files_properties(${XBE_SDK_DIR}/runtime/core/input.cpp PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")

# One executable per recompiled image: its code from config/<config> (partitions as that config sets them),
# the SDK runtime, XBE_HOST_SOURCES shared by every image of the game, and src/<game_dir> hooks only this image has.
# Expects assets/<xbe> and the config's out = "generated/<game_dir>". At run time the game may be a disc image instead.
function(add_xbe_image target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND $<TARGET_FILE:xbe_recomp> config/${config} --xdk ${XBE_SDK_DIR}/xdk
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;-Wno-parentheses-equality;-Wno-unused-variable")
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    add_executable(${target} WIN32 ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${XBE_IMGUI_SOURCES} ${XBE_HASH})
    add_dependencies(${target} recompile_${target})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    # mingw-w64 headers declare the Windows 10 API (DPI awareness contexts) only when asked
    if(MINGW)
        target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
        target_link_options(${target} PRIVATE -municode)  # the entry is wWinMain
    endif()
    # XAudio 2.9 (Windows 10): the Windows SDK names its import library xaudio2, mingw-w64 xaudio2_9
    if(MINGW)
        set(xaudio xaudio2_9)
    else()
        set(xaudio xaudio2)
    endif()
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> d3d12 dxgi dxguid d3dcompiler ${xaudio} ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    # lld keeps the recompiled weak functions overridable by the host ones
    target_link_options(${target} PRIVATE -fuse-ld=lld -g)
endfunction()

# Release without game code: what a player's PC needs to build the game from its own xbe, and nothing of the game.
#   <target>_runtime   static library: the SDK runtime, XBE_HOST_SOURCES, src/<game_dir> hooks and ImGui, built
#                      against image_symbols.h written from the config alone (xbe_recomp --symbols-only)
#   <target>_launcher  <target>.exe in release/: asks for the game, recompiles, compiles and links it with the
#                      bundled toolchain, then starts it (SDK launcher/main.cpp)
#   package_<target>   release/<target>/: the launcher, xbe_recomp, the libraries, headers, XDK layouts, config, the
#                      LLVM-MinGW toolchain pruned to x86_64, docs; zip it to publish
# The config's [xbe] sha1 names the xbe the release is made for. Needs the LLVM-MinGW toolchain (cmake/llvm-mingw.cmake),
# the one the player's build uses too.
function(add_xbe_release target xbe config game_dir)
    if(NOT MINGW)
        message(FATAL_ERROR "add_xbe_release: build with the LLVM-MinGW toolchain, the one a release bundles")
    endif()
    set(cfg_file ${CMAKE_CURRENT_SOURCE_DIR}/config/${config})
    file(READ ${cfg_file} cfg_text)
    string(REGEX MATCH "\nsha1 *= *\"([0-9a-fA-F]+)\"" _ "${cfg_text}")
    set(xbe_sha1 ${CMAKE_MATCH_1})
    if(NOT xbe_sha1)
        message(FATAL_ERROR "config/${config}: [xbe] sha1 = \"<the xbe's SHA-1>\" is needed for a release")
    endif()
    string(TOLOWER ${xbe_sha1} xbe_sha1)

    # image_symbols.h from the config
    set(SYM ${CMAKE_CURRENT_BINARY_DIR}/${target}_symbols)
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${SYM}/image_symbols.h
        COMMAND $<TARGET_FILE:xbe_recomp> config/${config} --symbols-only ${SYM} --xdk ${XBE_SDK_DIR}/xdk
        DEPENDS xbe_recomp ${configs}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Writing image_symbols.h from config/${config}"
        VERBATIM)

    # the runtime library
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_release_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    add_library(${target}_runtime STATIC ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${XBE_IMGUI_SOURCES}
        ${XBE_HASH} ${SYM}/image_symbols.h)
    target_include_directories(${target}_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${SYM})
    target_include_directories(${target}_runtime SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target}_runtime PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target}_runtime PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(${target}_runtime PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static>)

    # the launcher
    string(REGEX MATCH "\ntitle *= *(0x[0-9a-fA-F]+)" _ "${cfg_text}")
    set(LAUNCHER_INFO ${CMAKE_CURRENT_BINARY_DIR}/${target}_launcher_info.cpp)
    if(NOT XBE_RELEASE_VERSION)
        set(XBE_RELEASE_VERSION dev)
    endif()
    file(CONFIGURE OUTPUT ${LAUNCHER_INFO} CONTENT
"#include \"launcher.h\"
const launcher::Info launcher::kInfo = {
    .title = L\"${XBE_RELEASE_TITLE}\",
    .exe = L\"${target}\",
    .xbe = \"${xbe}\",
    .sha1 = \"${xbe_sha1}\",
    .config = L\"${config}\",
    .runtime = L\"lib${target}_runtime.a\",
    .version = \"${XBE_RELEASE_VERSION}\",
};
")
    add_executable(${target}_launcher WIN32 ${XBE_SDK_DIR}/launcher/main.cpp ${XBE_SDK_DIR}/launcher/build.cpp
        ${XBE_SDK_DIR}/launcher/window.cpp ${XBE_SDK_DIR}/runtime/kernel/disc.cpp ${XBE_SDK_DIR}/runtime/kernel/crypto.cpp
        ${LAUNCHER_INFO})
    target_include_directories(${target}_launcher PRIVATE ${XBE_SDK_DIR}/launcher ${XBE_SDK_DIR}/runtime)
    target_compile_definitions(${target}_launcher PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_options(${target}_launcher PRIVATE -municode -static)
    target_link_libraries(${target}_launcher PRIVATE comctl32 comdlg32 shell32 ole32)
    set_target_properties(${target}_launcher PROPERTIES OUTPUT_NAME ${target}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/release)

    # the player's compile and link lines; must match add_xbe_image's for the generated code
    set(XBE_PLAYER_COMPILE "-std=gnu++20 -O2 -march=x86-64-v2 -DNDEBUG -fno-strict-aliasing -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -Wno-unused-label -Wno-parentheses-equality -Wno-unused-variable")
    set(XBE_PLAYER_LINK "-static -mwindows -municode -fuse-ld=lld -ld3d12 -ldxgi -ldxguid -ld3dcompiler -lxaudio2_9 -lole32 -lwindowscodecs -ldbghelp -lwinmm -ldwmapi -lws2_32 -lm -lkernel32 -luser32 -lgdi32 -limm32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -lwinspool -lcomdlg32")

    set(PKG ${CMAKE_CURRENT_BINARY_DIR}/release/${target})
    get_filename_component(TOOLCHAIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    get_filename_component(TOOLCHAIN_DIR ${TOOLCHAIN_DIR} DIRECTORY)
    add_custom_target(package_${target}
        COMMAND ${CMAKE_COMMAND}
            -DPKG=${PKG} -DTARGET=${target} -DSDK=${XBE_SDK_DIR} -DGAME=${CMAKE_CURRENT_SOURCE_DIR} -DCONFIG=${config}
            -DLAUNCHER=$<TARGET_FILE:${target}_launcher> -DRECOMP=$<TARGET_FILE:xbe_recomp>
            -DRUNTIME=$<TARGET_FILE:${target}_runtime> -DSDL=$<TARGET_LINKER_FILE:SDL3::SDL3-static>
            -DSYMBOLS=${SYM}/image_symbols.h -DTOOLCHAIN=${TOOLCHAIN_DIR}
            "-DCOMPILE_FLAGS=${XBE_PLAYER_COMPILE}" "-DLINK_FLAGS=${XBE_PLAYER_LINK}"
            -P ${XBE_SDK_DIR}/cmake/XbePackage.cmake
        DEPENDS ${target}_launcher ${target}_runtime xbe_recomp
        COMMENT "Packaging release/${target}"
        VERBATIM)
endfunction()
