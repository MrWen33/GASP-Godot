@echo off
rem Set VSROOT (e.g. C:\Program Files\Microsoft Visual Studio\2022\Community) and optionally PYTHON before running.
if "%VSROOT%"=="" set VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community
if "%PYTHON%"=="" set PYTHON=python
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d %~dp0
if not exist build\build.ninja cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=%PYTHON% -DGODOTCPP_TARGET=template_debug
rem header dependency tracking: ninja must see the localized (GBK) /showIncludes prefix
%PYTHON% tools_fix_deps_prefix.py build\CMakeFiles\rules.ninja
cmake --build build --config Release
