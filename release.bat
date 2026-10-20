@echo off
rem The release package without game code, as the CI makes it (.github/workflows/build.yml), in
rem out\build\pkg\release\GenmaOnimusha and GenmaOnimusha-local-win64.zip.
rem LLVM_MINGW: the llvm-mingw-20260922-ucrt-x86_64 folder (https://github.com/mstorsjo/llvm-mingw/releases)
setlocal
cd /d "%~dp0"
if not defined LLVM_MINGW if exist "%~dp0..\tools\llvm-mingw-20260922-ucrt-x86_64" set "LLVM_MINGW=%~dp0..\tools\llvm-mingw-20260922-ucrt-x86_64"
if not defined LLVM_MINGW (echo Set LLVM_MINGW to the llvm-mingw folder first. & exit /b 1)
rem CMake and Ninja from Visual Studio's C++ CMake tools
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installerswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with C++ CMake tools was not found. & exit /b 1)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
cmake -S . -B out\build\pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DXBE_RELEASE=ON -DXBE_RELEASE_VERSION=local ^
    -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake "-DLLVM_MINGW=%LLVM_MINGW%" ^
    "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" || exit /b 1
cmake --build out\build\pkg --target package_GenmaOnimusha || exit /b 1
del /q GenmaOnimusha-local-win64.zip 2>nul
powershell -NoProfile -Command "Compress-Archive -Path out\build\pkg\release\GenmaOnimusha -DestinationPath GenmaOnimusha-local-win64.zip" || exit /b 1
echo GenmaOnimusha-local-win64.zip
