
@echo off
setlocal EnableExtensions

set "sdk_flag=false"
set "client_flag=false"
set "clean_flag=false"

:parse_args
if "%~1"=="" goto args_done

if /I "%~1"=="-s" (
    set "sdk_flag=true"
    shift
    goto parse_args
)

if /I "%~1"=="-c" (
    set "client_flag=true"
    shift
    goto parse_args
)

if /I "%~1"=="-x" (
    set "clean_flag=true"
    shift
    goto parse_args
)

if /I "%~1"=="-h" goto usage

echo Invalid option: %~1
goto usage_error

:args_done

if "%sdk_flag%"=="false" if "%client_flag%"=="false" (
    set "sdk_flag=true"
    set "client_flag=true"
)

if "%clean_flag%"=="true" (
    echo Cleaning build directories...

    if "%sdk_flag%"=="true" (
        if exist "build-sdk" rmdir /S /Q "build-sdk"
        if errorlevel 1 exit /b 1
    )

    if "%client_flag%"=="true" (
        if exist "build-client" rmdir /S /Q "build-client"
        if errorlevel 1 exit /b 1
    )
)

if "%sdk_flag%"=="true" (
    echo Building SDK...
    if not exist "build-sdk" mkdir "build-sdk"
    cd build-sdk
    cmake -G Ninja ../sdk
    cmake --build .
    if errorlevel 1 exit /b 1
)

if "%client_flag%"=="true" (
    echo Building Client...
    if not exist "build-client" mkdir "build-client"
    cd build-client
    cmake -G Ninja "-DMODLOADER_SDK=../build/dist"
    cmake --build .
    if errorlevel 1 exit /b 1
)

echo.
echo Build completed successfully.
exit /b 0

:usage
echo Usage: %~nx0 [-s] [-c] [-x] [-h]
echo.
echo Options:
echo   -s    Build SDK
echo   -c    Build Client
echo   -x    Clean build directories before building
echo   -h    Show this help message
exit /b 0

:usage_error
echo Usage: %~nx0 [-s] [-c] [-x] [-h]
echo.
echo Options:
echo   -s    Build SDK
echo   -c    Build Client
echo   -x    Clean build directories before building
echo   -h    Show this help message
exit /b 1
