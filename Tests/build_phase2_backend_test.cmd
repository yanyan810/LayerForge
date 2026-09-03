@echo off
setlocal
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W3 /utf-8 /I LayerForge Tests\Phase2BackendTest.cpp LayerForge\StyleAI\StyleAIBackend.cpp LayerForge\StyleAI\StyleTrainingConfig.cpp /Fe:Tests\Phase2BackendTest.exe
