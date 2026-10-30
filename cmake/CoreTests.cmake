# Compile the portable portion of the real SDK, without a window, SDL or a game image.
find_package(Threads REQUIRED)
add_library(xbe_portable_core STATIC
    ${SDK}/runtime/core/log.cpp
    ${SDK}/runtime/cpu/dispatch.cpp
    ${SDK}/runtime/kernel/crypto.cpp
    ${SDK}/runtime/kernel/disc.cpp
    ${SDK}/runtime/kernel/files.cpp
    ${SDK}/runtime/kernel/memory.cpp
    ${SDK}/runtime/kernel/objects.cpp
    ${SDK}/runtime/kernel/rtl.cpp
    ${SDK}/runtime/kernel/threads.cpp)
target_include_directories(xbe_portable_core PUBLIC ${SDK}/runtime)
target_compile_options(xbe_portable_core PRIVATE -fno-strict-aliasing)
target_compile_definitions(xbe_portable_core PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_portable_core PUBLIC Threads::Threads)

function(genma_core_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${SDK}/runtime)
    # Assertions are the checks in the existing regression programs, also in Release.
    target_compile_options(${name} PRIVATE -UNDEBUG -Wall -Wextra)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# Link only the exercised components: a static C++ library can otherwise select
# unrelated archive members through shared weak STL instantiations with lld.
# xbe_portable_core is still compiled in full by the default build.
genma_core_test(test_guest_memory tools/test_guest_memory.cpp
    ${SDK}/runtime/kernel/memory.cpp ${SDK}/runtime/core/log.cpp)
target_compile_options(test_guest_memory PRIVATE -fno-strict-aliasing)
target_link_libraries(test_guest_memory PRIVATE Threads::Threads)
genma_core_test(test_disc_portable tools/test_disc_portable.cpp
    ${SDK}/runtime/kernel/disc.cpp ${SDK}/runtime/core/log.cpp)
target_link_libraries(test_disc_portable PRIVATE Threads::Threads)
genma_core_test(test_cpu_abi tools/test_cpu_abi.cpp tools/test_cpu_abi_weak.cpp)
genma_core_test(test_modern_controls tools/test_modern_controls.cpp)
genma_core_test(test_audio_distance tools/test_audio_distance.cpp)

genma_core_test(test_frame_clock tools/test_frame_clock.cpp)
target_link_libraries(test_frame_clock PRIVATE Threads::Threads)

genma_core_test(test_disk_cache tools/test_disk_cache.cpp)
target_link_libraries(test_disk_cache PRIVATE Threads::Threads)

genma_core_test(test_resource_cache tools/test_resource_cache.cpp)
target_link_libraries(test_resource_cache PRIVATE Threads::Threads)
genma_core_test(test_xpr_index tools/test_xpr_index.cpp)

# Exercise the production hook with symbols derived from config alone, never an XBE.
set(test_symbols ${CMAKE_CURRENT_BINARY_DIR}/core_test_symbols)
add_custom_command(OUTPUT ${test_symbols}/image_symbols.h
    COMMAND $<TARGET_FILE:xbe_recomp> config/game.toml --symbols-only ${test_symbols} --xdk ${SDK}/xdk
    DEPENDS xbe_recomp ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} VERBATIM)
add_custom_target(genma_core_test_symbols DEPENDS ${test_symbols}/image_symbols.h)

genma_core_test(test_modern_controls_hook tools/test_modern_controls_hook.cpp)
add_dependencies(test_modern_controls_hook genma_core_test_symbols)
target_include_directories(test_modern_controls_hook PRIVATE ${test_symbols})

genma_core_test(test_background_streaming_hook tools/test_background_streaming_hook.cpp)
add_dependencies(test_background_streaming_hook genma_core_test_symbols)
target_include_directories(test_background_streaming_hook PRIVATE ${test_symbols})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    genma_core_test(test_linux_fiber tools/test_linux_fiber.cpp ${SDK}/runtime/core/log.cpp)
    target_link_libraries(test_linux_fiber PRIVATE Threads::Threads)
endif()
