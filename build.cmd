@echo off
setlocal
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
msbuild LayerForge.sln /m /p:Configuration=%1 /p:Platform=x64 /v:minimal
