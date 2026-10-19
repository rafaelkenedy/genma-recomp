# Toolchain: LLVM-MinGW (clang, lld, libc++, mingw-w64), the same compiler a release bundles for the player's build.
# LLVM_MINGW points at its folder; by default tools/llvm-mingw beside this repository's parent.
if(NOT LLVM_MINGW)
    set(LLVM_MINGW $ENV{LLVM_MINGW})
endif()
if(NOT LLVM_MINGW)
    message(FATAL_ERROR "set LLVM_MINGW to the llvm-mingw folder (cmake -DLLVM_MINGW=... or the environment)")
endif()
# forward slashes: a Windows path's backslashes would read as escapes in the files CMake writes
file(TO_CMAKE_PATH "${LLVM_MINGW}" LLVM_MINGW)
# try_compile projects see it too
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW)
set(ENV{LLVM_MINGW} ${LLVM_MINGW})
set(CMAKE_C_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang.exe)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang++.exe)
set(CMAKE_RC_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-windres.exe)
set(CMAKE_AR ${LLVM_MINGW}/bin/llvm-ar.exe)
set(CMAKE_RANLIB ${LLVM_MINGW}/bin/llvm-ranlib.exe)
# everything static: the exe needs no libc++ or winpthread DLL beside it
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
