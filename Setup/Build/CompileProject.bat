@echo off
rem Builds the editor of a C++ project (<Name>Editor.exe) from the editor's Compile button.
rem Usage: Setup\Build\CompileProject.bat Path\To\Game.hproject <Name> [Debug^|Release^|...]
rem   1) Regenerates the project solution so new source files are picked up (premake only rewrites changed files).
rem   2) Builds <Name>Editor with MSBuild. Output goes to <ProjectDir>\Build\Bin\<Config>-windows-x86_64\.
setlocal
if "%~2"=="" (
    echo [Compile] Usage: CompileProject.bat Path\To\Game.hproject Name [Config]
    exit /b 1
)
set "PROJECT=%~f1"
set "NAME=%~2"
set "CONFIG=%~3"
if "%CONFIG%"=="" set "CONFIG=Debug"
for %%P in ("%PROJECT%") do set "PROJECT_DIR=%%~dpP"

rem Build messages in English so they are not garbled when read through a pipe (editor Output Log).
set "VSLANG=1033"

echo [Compile] Generating project files...
call "%~dp0GenerateProjectFiles.bat" "%PROJECT%" --no-pause
if errorlevel 1 (
    echo [Compile] Generating project files failed.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%M in (`"%VSWHERE%" -latest -prerelease -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%M"
if not defined MSBUILD (
    echo [Compile] MSBuild not found. Install Visual Studio with the C++ workload.
    exit /b 1
)

echo [Compile] Building %NAME%Editor (%CONFIG%)...
"%MSBUILD%" "%PROJECT_DIR%Build\Projects\%NAME%Editor.vcxproj" -p:Configuration=%CONFIG% -p:Platform=x64 -m -nologo -v:minimal
if errorlevel 1 (
    echo [Compile] Build failed.
    exit /b 1
)
echo [Compile] Build succeeded.
exit /b 0
