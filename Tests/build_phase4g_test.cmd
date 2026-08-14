@echo off
setlocal
set "PATH="
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
set "ORT=%USERPROFILE%\.nuget\packages\microsoft.ml.onnxruntime.directml\1.24.4"
set "DML=%USERPROFILE%\.nuget\packages\microsoft.ai.directml\1.15.4"
cl /nologo /EHsc /std:c++20 /utf-8 /O2 /I LayerForge /I "%ORT%\build\native\include" ^
  Tests\Phase4GAsyncTest.cpp LayerForge\ImageLoader.cpp LayerForge\AI\AIModelManager.cpp ^
  LayerForge\AI\SegmentationModel.cpp LayerForge\AI\MaskProcessor.cpp LayerForge\AI\GroundingDinoModel.cpp ^
  LayerForge\AI\HairBoxFilter.cpp LayerForge\AI\InferenceDevice.cpp LayerForge\AI\Sam2Model.cpp ^
  /Fe:x64\Release\Phase4GAsyncTest.exe /link /LIBPATH:"%ORT%\runtimes\win-x64\native" onnxruntime.lib windowscodecs.lib ole32.lib
if errorlevel 1 exit /b %errorlevel%
copy /Y "%ORT%\runtimes\win-x64\native\onnxruntime.dll" "x64\Release\onnxruntime.dll" >nul
copy /Y "%ORT%\runtimes\win-x64\native\onnxruntime_providers_shared.dll" "x64\Release\onnxruntime_providers_shared.dll" >nul
copy /Y "%DML%\bin\x64-win\DirectML.dll" "x64\Release\DirectML.dll" >nul
