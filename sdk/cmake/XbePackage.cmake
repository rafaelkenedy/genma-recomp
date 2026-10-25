# cmake -P: lays out a release without game code in PKG (see add_xbe_release). Variables:
#   PKG TARGET SDK GAME CONFIG LAUNCHER RECOMP RUNTIME SDL SYMBOLS TOOLCHAIN COMPILE_FLAGS LINK_FLAGS
cmake_minimum_required(VERSION 3.25)

file(REMOVE_RECURSE ${PKG})
file(MAKE_DIRECTORY ${PKG}/assets ${PKG}/sdk/bin ${PKG}/sdk/lib ${PKG}/sdk/include ${PKG}/sdk/config)

# the launcher and the recompiler
file(COPY ${LAUNCHER} DESTINATION ${PKG})
file(COPY ${RECOMP} DESTINATION ${PKG}/sdk/bin)
file(COPY ${SDK}/launcher/update.ps1 DESTINATION ${PKG}/sdk)
file(WRITE ${PKG}/sdk/update-version.txt "${VERSION}\n")

# what the generated code is compiled and linked against
file(COPY ${RUNTIME} ${SDL} DESTINATION ${PKG}/sdk/lib)
file(COPY ${SDK}/runtime/ DESTINATION ${PKG}/sdk/include FILES_MATCHING PATTERN "*.h")
file(COPY ${SYMBOLS} DESTINATION ${PKG}/sdk/include)
file(COPY ${SDK}/xdk DESTINATION ${PKG}/sdk)
file(COPY ${GAME}/config/${CONFIG} DESTINATION ${PKG}/sdk/config)
file(WRITE ${PKG}/sdk/compile.txt "${COMPILE_FLAGS}\n")
file(WRITE ${PKG}/sdk/link.txt "${LINK_FLAGS}\n")

# documents
foreach(f README.md LICENSE THIRD_PARTY.txt)
    if(EXISTS ${GAME}/${f})
        file(COPY ${GAME}/${f} DESTINATION ${PKG})
    endif()
endforeach()
file(COPY ${SDK}/THIRD_PARTY.txt DESTINATION ${PKG}/sdk)
# HD texture replacements: beside the game exe the launcher builds into game/
if(EXISTS ${GAME}/textures)
    file(COPY ${GAME}/textures DESTINATION ${PKG}/game)
endif()
file(COPY ${SDK}/LICENSE DESTINATION ${PKG}/sdk)
file(WRITE ${PKG}/assets/PUT_YOUR_GAME_HERE.txt
    "Put your own copy of the game here: its disc image (.iso), or default.xbe with the game's folders beside it.\n"
    "You can also start the exe and pick the .iso when it asks.\n")

# LLVM-MinGW, x86_64 only: clang, lld, their DLLs, headers, the x86_64 sysroot and compiler-rt builtins
set(T ${PKG}/toolchain)
file(MAKE_DIRECTORY ${T}/bin ${T}/lib)
file(GLOB bin_keep
    ${TOOLCHAIN}/bin/clang.exe ${TOOLCHAIN}/bin/clang++.exe ${TOOLCHAIN}/bin/clang-[0-9]*.exe
    ${TOOLCHAIN}/bin/ld.lld.exe ${TOOLCHAIN}/bin/lld.exe ${TOOLCHAIN}/bin/x86_64-w64-mingw32-clang++.exe
    ${TOOLCHAIN}/bin/x86_64-w64-mingw32-clang.exe ${TOOLCHAIN}/bin/x86_64-w64-mingw32-ld
    ${TOOLCHAIN}/bin/*.cfg ${TOOLCHAIN}/bin/libLLVM-*.dll ${TOOLCHAIN}/bin/libclang-cpp.dll ${TOOLCHAIN}/bin/libc++.dll
    ${TOOLCHAIN}/bin/libunwind.dll ${TOOLCHAIN}/bin/libwinpthread-1.dll)
file(COPY ${bin_keep} DESTINATION ${T}/bin)
file(GLOB cfg_drop ${T}/bin/aarch64* ${T}/bin/arm* ${T}/bin/i686*)
if(cfg_drop)
    file(REMOVE ${cfg_drop})
endif()
file(COPY ${TOOLCHAIN}/include DESTINATION ${T})
file(COPY ${TOOLCHAIN}/x86_64-w64-mingw32 DESTINATION ${T})
file(GLOB clang_ver LIST_DIRECTORIES true ${TOOLCHAIN}/lib/clang/*)
get_filename_component(ver ${clang_ver} NAME)
file(COPY ${clang_ver}/include DESTINATION ${T}/lib/clang/${ver})
file(COPY ${clang_ver}/lib/windows/libclang_rt.builtins-x86_64.a DESTINATION ${T}/lib/clang/${ver}/lib/windows)
file(COPY ${TOOLCHAIN}/LICENSE.TXT DESTINATION ${T})

message(STATUS "release laid out in ${PKG}")
