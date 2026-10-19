@echo off
rem runs a command inside the Visual Studio x64 environment with LLVM first on PATH
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
cd /d "%~dp0"
%*
