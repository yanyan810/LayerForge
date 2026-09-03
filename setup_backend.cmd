@echo off
setlocal
if not defined PYTHON_EXE set "PYTHON_EXE=C:\Users\yanya\AppData\Local\Python\pythoncore-3.12-64\python.exe"
if not exist "%PYTHON_EXE%" set "PYTHON_EXE=python"
if not exist "runtime\python\Scripts\python.exe" "%PYTHON_EXE%" -m venv "runtime\python"
if errorlevel 1 exit /b %errorlevel%
"runtime\python\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 exit /b %errorlevel%
"runtime\python\Scripts\python.exe" -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128
if errorlevel 1 exit /b %errorlevel%
"runtime\python\Scripts\python.exe" -m pip install -r backend\requirements.txt
if errorlevel 1 exit /b %errorlevel%
"runtime\python\Scripts\python.exe" -c "import torch; print('PyTorch', torch.__version__); print('CUDA available:', torch.cuda.is_available())"
