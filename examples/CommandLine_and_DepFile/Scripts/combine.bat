@echo off
setlocal

:: Set variables
set "file_name=%~1"
set "source_folder=%~2"
set "output_folder=%~3"
set "dependency_folder=%~4"

:: Remove trailing backslash from folder paths if present
if "%source_folder:~-1%"=="\" set "source_folder=%source_folder:~0,-1%"
if "%output_folder:~-1%"=="\" set "output_folder=%output_folder:~0,-1%"
if "%dependency_folder:~-1%"=="\" set "dependency_folder=%dependency_folder:~0,-1%"

set "r_file=%source_folder%%file_name%.txt"
set "g_file=%r_file:_r=_g%"
set "out_file=%output_folder%%file_name:_r=_out%.txt"

:: Check if _r file exists
if not exist "%r_file%" (
    exit /b 1
)

:: Check if _g file exists
if not exist "%g_file%" (
    exit /b 1
)

:: Check if output folder exists
if not exist "%output_folder%" (
    exit /b 1
)

:: Combine the contents into the output file
(
    type "%r_file%"
    type "%g_file%"
) > "%out_file%"

if %errorlevel% neq 0 (
    exit /b 1
)

:: Create dependency file
echo INPUT: data\source\%file_name:_r=_g%.txt > "%dependency_folder%\%file_name%.dep"
echo OUTPUT: data\bin\%file_name:_r=_out%.txt >> "%dependency_folder%\%file_name%.dep"

endlocal
