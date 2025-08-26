@echo off
setlocal enabledelayedexpansion

REM Usage: generate_version_header.bat <version_file_path> <output_folder>

if "%~1"=="" (
    echo ERROR: Version file path not specified.
    exit /b 1
)

if "%~2"=="" (
    echo ERROR: Output folder not specified.
    exit /b 1
)

set "versionFile=%~1"
set "outputDir=%~2"
set "headerFile=%outputDir%\version.h"

if not exist "%versionFile%" (
    echo ERROR: Version file "%versionFile%" not found!
    exit /b 1
)

if not exist "%outputDir%" (
    echo ERROR: Output folder "%outputDir%" not found!
    exit /b 1
)

set "version="

REM Parse version from the first non-comment, non-empty key=value line
for /f "usebackq tokens=1,* delims== " %%a in ("%versionFile%") do (
    set "key=%%a"
    set "val=%%b"

    REM Trim spaces
    for /f "tokens=* delims= " %%x in ("!key!") do set "key=%%x"
    for /f "tokens=* delims= " %%x in ("!val!") do set "val=%%x"

    REM Skip empty lines or comment lines
    if not "!key!"=="" if not "!key:~0,2!"=="//" if not "!key:~0,2!"=="/*" if not "!key:~0,1!"=="#" (
        set "version=!val!"
        goto :gotVersion
    )
)

:gotVersion

if not defined version (
    echo ERROR: Version not found in file!
    exit /b 1
)

REM Get Jenkins build number if defined
if defined BUILD_NUMBER (
    set "buildNumber=%BUILD_NUMBER%"
) else (
    set "buildNumber="
)

REM Compose full version string
if defined buildNumber (
    set "fullVersion=%version%.%buildNumber%"
) else (
    set "fullVersion=%version%"
)

echo Generating version header at "%headerFile%"
(
    echo #pragma once
    echo #define FILE_VERSION %fullVersion:.=, %
    echo #define PRODUCT_VERSION %fullVersion:.=, %
    echo #define FILE_VERSION_STR "%fullVersion%\0"
    echo #define PRODUCT_VERSION_STR "%fullVersion%\0"
) > "%headerFile%"

echo Done.
endlocal
exit /b 0
