@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Distant Streaming: fast incremental build after the first successful setup.
rem   build.bat          = fast path when configured; full setup automatically on first run
rem   build.bat fast     = fast path only (no dependency upgrades or auto-bootstrap)
rem   build.bat cache64  = force rebuild of the x64 helper, then build the ASI
rem   build.bat full     = full dependency bootstrap + SDK + x64 + Win32 builds
rem   build.bat "SDK" "SDK_LIB" "VCPKG" = explicit dependency paths (full path)
rem For the full path:
rem   ASC_SKIP_UPGRADES=1    : do not check/update installed dependencies
rem   ASC_OFFLINE=1          : reuse locally installed dependencies only
rem   ASC_UPDATE_EXTERNAL=1 : allow git updates for explicitly supplied repos

set "PROJECT_ROOT=%~dp0"
if not exist "%PROJECT_ROOT%tools\bootstrap-deps.ps1" (
    echo [ERROR] Missing tools\bootstrap-deps.ps1
    exit /b 1
)
where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Windows PowerShell 5.1 is required.
    exit /b 1
)

if /I "%~1"=="full" goto FULL_NOARGS
if /I "%~1"=="cache64" goto FORCE_CACHE64
if /I "%~1"=="fast" goto FAST_REQUIRED
if not "%~1"=="" goto FULL_WITHARGS

echo [build] Using fast incremental build if already configured...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\fast-build.ps1"
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" goto SUCCESS
if "%RESULT%"=="20" goto FULL_NOARGS
exit /b %RESULT%

:FAST_REQUIRED
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\fast-build.ps1"
exit /b %ERRORLEVEL%

:FORCE_CACHE64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\fast-build.ps1" -ForceCache64
exit /b %ERRORLEVEL%

:FULL_WITHARGS
echo [build] Full dependency bootstrap with explicit paths...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\bootstrap-deps.ps1" %*
goto FULL_RESULT

:FULL_NOARGS
echo [build] Full dependency bootstrap (first setup or explicitly requested)...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\bootstrap-deps.ps1"
goto FULL_RESULT

:FULL_RESULT
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [ERROR] Build or dependency bootstrap failed: %RESULT%
    exit /b %RESULT%
)

:SUCCESS
echo [OK] Build completed. Output is in dist\
exit /b 0
