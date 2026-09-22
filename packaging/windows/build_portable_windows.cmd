@echo off
setlocal
rem Repo convention:
rem - build trees and CPack staging live under %TEMP%
rem - the repository release\ directory stores only ready-to-use packages
rem
rem   build_portable_windows.cmd <cmake-platform> <package-suffix> [build-type]
rem   build_portable_windows.cmd x64 win64
rem   build_portable_windows.cmd x64 win64_shared shared
rem   build_portable_windows.cmd x64 win64_both both
rem
rem Optional environment variables:
rem   CMAKE_GENERATOR_NAME  default "Visual Studio 17 2022"

if "%~2"=="" (
    echo Usage: %~nx0 ^<cmake-platform^> ^<package-suffix^> [build-type]
    echo Example: %~nx0 x64 win64
    echo Example: %~nx0 x64 win64_shared shared
    exit /b 1
)

for %%I in ("%~dp0..\..") do set "PROJECT_ROOT=%%~fI"

set "CMAKE_PLATFORM=%~1"
set "PACKAGE_SUFFIX=%~2"
set "RAW_BUILD_TYPE=%~3"
if "%CMAKE_GENERATOR_NAME%"=="" set "CMAKE_GENERATOR_NAME=Visual Studio 17 2022"

set "BUILD_MODE=static"
set "CMAKE_MODE_ARG=-DBUILD_SHARED_LIBS=OFF"
if /i "%RAW_BUILD_TYPE%"=="shared" (
    set "BUILD_MODE=shared"
    set "CMAKE_MODE_ARG=-DBUILD_SHARED_LIBS=ON"
)
if /i "%RAW_BUILD_TYPE%"=="dynamic" (
    set "BUILD_MODE=shared"
    set "CMAKE_MODE_ARG=-DBUILD_SHARED_LIBS=ON"
)
if /i "%RAW_BUILD_TYPE%"=="both" (
    set "BUILD_MODE=both"
    set "CMAKE_MODE_ARG=-DXXFC_BUILD_BOTH=ON"
)

set "WORK_ROOT=%TEMP%\xxfclib_%PACKAGE_SUFFIX%"
set "BUILD_DIR=%WORK_ROOT%\build"
set "PACKAGE_BASENAME=xxfclib_%PACKAGE_SUFFIX%_portable"

if not exist "%PROJECT_ROOT%\release_version.txt" (
    echo release_version.txt not found:
    echo   %PROJECT_ROOT%\release_version.txt
    exit /b 1
)

set /p RELEASE_VERSION=<"%PROJECT_ROOT%\release_version.txt"
set "PACKAGE_NAME=%PACKAGE_BASENAME%_%RELEASE_VERSION%"
set "RELEASE_DIR=%PROJECT_ROOT%\release"
set "PACKAGE_DIR=%RELEASE_DIR%\%PACKAGE_NAME%"
set "CPACK_DIR=%WORK_ROOT%\cpack"
set "CPACK_OUTPUT_DIR=%WORK_ROOT%\output"

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"
mkdir "%WORK_ROOT%"
if errorlevel 1 exit /b 1

echo Configuring %PACKAGE_SUFFIX% build (mode: %BUILD_MODE%)...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" -A %CMAKE_PLATFORM% %CMAKE_MODE_ARG% "-DCPACK_PACKAGE_FILE_NAME=%PACKAGE_NAME%"
if errorlevel 1 exit /b 1

echo Building %PACKAGE_SUFFIX% Release...
cmake --build "%BUILD_DIR%" --config Release --clean-first
if errorlevel 1 exit /b 1

set "CPACK_CONFIG=%BUILD_DIR%\CPackConfig.cmake"

if "%BUILD_MODE%"=="shared" (
    if not exist "%BUILD_DIR%\Release\xxfclib.dll" (
        echo Built shared DLL not found: %BUILD_DIR%\Release\xxfclib.dll
        exit /b 1
    )
    if not exist "%BUILD_DIR%\Release\xxfclib.lib" (
        echo Built import library not found: %BUILD_DIR%\Release\xxfclib.lib
        exit /b 1
    )
) else if "%BUILD_MODE%"=="both" (
    if not exist "%BUILD_DIR%\Release\xxfclib.dll" (
        echo Built shared DLL not found: %BUILD_DIR%\Release\xxfclib.dll
        exit /b 1
    )
    if not exist "%BUILD_DIR%\Release\xxfclib.lib" (
        echo Built import library not found: %BUILD_DIR%\Release\xxfclib.lib
        exit /b 1
    )
    if not exist "%BUILD_DIR%\Release\xxfclib_static.lib" (
        echo Built static library not found: %BUILD_DIR%\Release\xxfclib_static.lib
        exit /b 1
    )
) else (
    if not exist "%BUILD_DIR%\Release\xxfclib.lib" (
        echo Built library not found: %BUILD_DIR%\Release\xxfclib.lib
        exit /b 1
    )
)

if not exist "%CPACK_CONFIG%" (
    echo CPack config not found:
    echo   %CPACK_CONFIG%
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
if exist "%CPACK_DIR%" rmdir /s /q "%CPACK_DIR%"
if exist "%CPACK_OUTPUT_DIR%" rmdir /s /q "%CPACK_OUTPUT_DIR%"

echo Installing portable package folder...
cmake --install "%BUILD_DIR%" --config Release --prefix "%PACKAGE_DIR%"
if errorlevel 1 exit /b 1

echo Creating portable zip with CPack...
cpack --config "%CPACK_CONFIG%" -G ZIP -C Release -B "%CPACK_DIR%" -D "CPACK_OUTPUT_FILE_PREFIX=%CPACK_OUTPUT_DIR%"
if errorlevel 1 exit /b 1

set "CPACK_ZIP="
for /r "%CPACK_OUTPUT_DIR%" %%F in (*.zip) do (
    set "CPACK_ZIP=%%~fF"
    goto cpack_zip_found
)

echo CPack did not produce a zip archive in:
    echo   %CPACK_OUTPUT_DIR%
exit /b 1

:cpack_zip_found
set "PACKAGE_ZIP=%RELEASE_DIR%\%PACKAGE_NAME%.zip"
if exist "%PACKAGE_ZIP%" del /f /q "%PACKAGE_ZIP%"
copy /y "%CPACK_ZIP%" "%PACKAGE_ZIP%" >nul
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"

echo.
echo Portable package folder created:
echo   %PACKAGE_DIR%
echo Portable zip created by CPack:
echo   %PACKAGE_ZIP%

endlocal
