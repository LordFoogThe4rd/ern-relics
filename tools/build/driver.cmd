@echo off
setlocal
call "%~2\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
cd /d "%~1"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~3" --preset %~5 -B "%~8" -DCMAKE_MAKE_PROGRAM="%~4" -DPython3_EXECUTABLE="%~6"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if "%~7"=="0" (
    "%~3" --build "%~8"
) else (
    "%~3" --build "%~8" --parallel %~7
)
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~dp3ctest.exe" --test-dir "%~8" --output-on-failure --no-tests=error
if not "%errorlevel%"=="0" exit /b %errorlevel%
exit /b %errorlevel%
