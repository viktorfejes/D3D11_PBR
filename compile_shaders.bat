@echo off
setlocal enabledelayedexpansion

:: Ensure we are in the right directory, no matter where we call this script from.
cd /D "%~dp0"

:: Set FXC path and flags
set fxc= "c:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe"
set fxc_flags= /nologo /WX /0d /Zi



clang++ -Wall -Werror -Wextra -pedantic ^
    -std=c++20 ^
    -g -O0 ^
    -fsanitize=address,undefined ^
    -fno-sanitize=vptr ^
    -Wno-defaulted-function-deleted ^
    -D_DEBUG ^
    -Ideps\DirectXMath\Inc -Ideps\tinyobjloader -Ideps\stb -Ideps\cgltf ^
    src\main.cpp ^
    src\application.cpp src\window.cpp src\renderer.cpp ^
    src\shader.cpp src\mesh.cpp src\camera.cpp ^
    src\input.cpp src\material.cpp src\texture.cpp ^
    src\id.cpp src\scene.cpp ^
    -ld3d11 -ld3dcompiler ^
    -o build\pbr_debug.exe

set "build_result=%errorlevel%"

:: Finish
if %build_result% EQU 0 (
    echo Build successful! Output: build\pbr_debug.exe
) else (
    echo Build failed with error code %build_result%.
)

endlocal
exit /b %build_result%


