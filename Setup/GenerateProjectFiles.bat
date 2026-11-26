@echo off
rem Generates the Visual Studio solution of a C++ project (like UE "Generate Visual Studio project files").
rem Usage: Setup\GenerateProjectFiles.bat Path\To\Game.hproject [--no-pause]
rem Creates <Name>.slnx and Intermediate\ProjectFiles\*.vcxproj in the project folder.
setlocal
if "%~1"=="" (
    echo Usage: GenerateProjectFiles.bat Path\To\Game.hproject [--no-pause]
    exit /b 1
)

rem SetupFbxSdk stores FBX_SDK_ROOT as a user environment variable. Read it from the registry if this console lacks it.
if "%FBX_SDK_ROOT%"=="" (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v FBX_SDK_ROOT 2^>nul ^| find "FBX_SDK_ROOT"') do set "FBX_SDK_ROOT=%%B"
)

"%~dp0premake5.exe" --file="%~dp0ProjectSolution.lua" --project="%~f1" vs2026
set "GenerateExitCode=%ERRORLEVEL%"
if /I not "%~2"=="--no-pause" pause
exit /b %GenerateExitCode%
