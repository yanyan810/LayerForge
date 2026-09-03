@echo off
setlocal
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++20 /utf-8 /O2 /I LayerForge ^
  Tests\MaskEditorTest.cpp LayerForge\Editor\MaskEditor.cpp LayerForge\Editor\SmartMaskCorrection.cpp ^
  /Fe:x64\Release\MaskEditorTest.exe
