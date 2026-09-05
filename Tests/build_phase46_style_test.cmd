@echo off
setlocal
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W3 /utf-8 /I LayerForge Tests\Phase46StylePresetTest.cpp LayerForge\StyleAI\StylePreset.cpp /Fe:Tests\Phase46StylePresetTest.exe
if errorlevel 1 exit /b %errorlevel%
Tests\Phase46StylePresetTest.exe
