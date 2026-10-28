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

genma_core_test(test_guest_memory tools/test_guest_memory.cpp)
target_link_libraries(test_guest_memory PRIVATE xbe_portable_core)
genma_core_test(test_disc_portable tools/test_disc_portable.cpp)
target_link_libraries(test_disc_portable PRIVATE xbe_portable_core)
genma_core_test(test_cpu_abi tools/test_cpu_abi.cpp tools/test_cpu_abi_weak.cpp)
genma_core_test(test_modern_controls tools/test_modern_controls.cpp)
genma_core_test(test_audio_distance tools/test_audio_distance.cpp)

# Exercise the production hook with symbols derived from config alone, never an XBE.
set(test_symbols ${CMAKE_CURRENT_BINARY_DIR}/core_test_symbols)
add_custom_command(OUTPUT ${test_symbols}/image_symbols.h
    COMMAND $<TARGET_FILE:xbe_recomp> config/game.toml --symbols-only ${test_symbols} --xdk ${SDK}/xdk
    DEPENDS xbe_recomp ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} VERBATIM)
genma_core_test(test_modern_controls_hook tools/test_modern_controls_hook.cpp ${test_symbols}/image_symbols.h)
target_include_directories(test_modern_controls_hook PRIVATE ${test_symbols})
