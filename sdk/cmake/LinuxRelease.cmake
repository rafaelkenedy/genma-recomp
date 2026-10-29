include_guard(GLOBAL)
function(add_xbe_linux_release target xbe config game_dir)
    if(NOT XBE_LINUX_DEPENDENCIES OR NOT EXISTS "${XBE_LINUX_DEPENDENCIES}/ready.txt")
        message(FATAL_ERROR "Prepare pinned dependencies with tools/prepare_linux_dependencies.py and set XBE_LINUX_DEPENDENCIES")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(cfg_file "${CMAKE_CURRENT_SOURCE_DIR}/config/${config}")
    file(READ "${cfg_file}" text)
    string(REGEX MATCH "\nsha1 *= *\"([0-9a-fA-F]+)\"" _ "${text}")
    set(sha1 "${CMAKE_MATCH_1}")
    if(NOT sha1)
        message(FATAL_ERROR "Release config needs the XBE SHA-1")
    endif()
    if(NOT XBE_RELEASE_VERSION)
        set(XBE_RELEASE_VERSION dev)
    endif()
    if(NOT XBE_RELEASE_COMMIT)
        execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE XBE_RELEASE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endif()
    set(sym "${CMAKE_CURRENT_BINARY_DIR}/${target}_symbols")
    file(GLOB configs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml" "${XBE_SDK_DIR}/xdk/*/*.toml")
    add_custom_command(OUTPUT "${sym}/image_symbols.h"
        COMMAND $<TARGET_FILE:xbe_recomp> "${cfg_file}" --symbols-only "${sym}" --xdk "${XBE_SDK_DIR}/xdk"
        DEPENDS xbe_recomp ${configs} VERBATIM)
    set(rt ${XBE_RUNTIME_SOURCES})
    list(FILTER rt EXCLUDE REGEX "/wsl_controller_bridge\\.cpp$")
    file(GLOB hooks CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp")
    list(FILTER hooks EXCLUDE REGEX "/acos_experiment\\.cpp$")
    set(hash "${CMAKE_CURRENT_BINARY_DIR}/${target}_release_xbe.cpp")
    file(CONFIGURE OUTPUT "${hash}" CONTENT "extern const char kImageSha1[] = \"${sha1}\";\n")
    add_library(${target}_runtime STATIC ${rt} ${XBE_HOST_SOURCES} ${hooks} "${hash}" "${sym}/image_symbols.h")
    target_include_directories(${target}_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XBE_SDK_DIR}/runtime" "${sym}")
    target_compile_definitions(${target}_runtime PRIVATE XBE_RAW_VERTICES=0 XBE_DISABLE_CONTROLLER_BRIDGE=1)
    target_compile_options(${target}_runtime PRIVATE -fno-strict-aliasing -march=x86-64-v2)
    target_link_libraries(${target}_runtime PRIVATE tomlplusplus::tomlplusplus SDL3::SDL3-static Vulkan::Vulkan)
    set(info "${CMAKE_CURRENT_BINARY_DIR}/${target}_linux_info.cpp")
    file(CONFIGURE OUTPUT "${info}" CONTENT
"#include \"linux_info.h\"
const linux_launcher::Info linux_launcher::kInfo = {\"${target}\", \"${xbe}\", \"${sha1}\", \"${XBE_RELEASE_VERSION}\"};
")
    add_executable(${target}_launcher "${XBE_SDK_DIR}/launcher/linux.cpp" "${info}"
        "${XBE_SDK_DIR}/runtime/kernel/disc.cpp" "${XBE_SDK_DIR}/runtime/kernel/crypto.cpp"
        "${XBE_SDK_DIR}/runtime/core/log.cpp")
    target_include_directories(${target}_launcher PRIVATE "${XBE_SDK_DIR}/launcher" "${XBE_SDK_DIR}/runtime")
    target_compile_options(${target}_launcher PRIVATE -march=x86-64-v2)
    target_link_options(${target}_launcher PRIVATE -fuse-ld=lld -static-libstdc++ -static-libgcc)
    target_link_libraries(${target}_launcher PRIVATE pthread)
    set_target_properties(${target}_launcher PROPERTIES OUTPUT_NAME "${target}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/launcher")
    add_custom_target(package_${target}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_linux.py"
            --output "${CMAKE_CURRENT_BINARY_DIR}/release/${target}"
            --launcher $<TARGET_FILE:${target}_launcher> --recompiler $<TARGET_FILE:xbe_recomp>
            --runtime $<TARGET_FILE:${target}_runtime> --sdl $<TARGET_LINKER_FILE:SDL3::SDL3-static>
            --symbols "${sym}/image_symbols.h" --sdk "${XBE_SDK_DIR}"
            --project "${CMAKE_CURRENT_SOURCE_DIR}" --dependencies "${XBE_LINUX_DEPENDENCIES}"
            --version "${XBE_RELEASE_VERSION}" --commit "${XBE_RELEASE_COMMIT}"
        DEPENDS ${target}_launcher ${target}_runtime xbe_recomp VERBATIM)
endfunction()
