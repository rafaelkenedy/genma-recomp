if(NOT XBE_LINUX_DEPENDENCIES)
    message(FATAL_ERROR "Set XBE_LINUX_DEPENDENCIES to the output of tools/prepare_linux_dependencies.py")
endif()
file(TO_CMAKE_PATH "${XBE_LINUX_DEPENDENCIES}" XBE_LINUX_DEPENDENCIES)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XBE_LINUX_DEPENDENCIES)
set(CMAKE_C_COMPILER "${XBE_LINUX_DEPENDENCIES}/toolchain/bin/clang")
set(CMAKE_CXX_COMPILER "${XBE_LINUX_DEPENDENCIES}/toolchain/bin/clang++")
# SDL/Vulkan use host development headers. Keep pinned libc/C++ headers first;
# these fallbacks are build-only and are never passed to the packaged compiler.
set(CMAKE_C_FLAGS_INIT "-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu")
set(CMAKE_CXX_FLAGS_INIT "-idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libstdc++ -static-libgcc")
