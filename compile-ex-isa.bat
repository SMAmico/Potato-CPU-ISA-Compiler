@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "script_dir=%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help
if "%~1"=="" goto :usage_error

set "output="
set "asm_output="
set "assembler="
set "assembler_args="
set /a source_count=0

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="-o" goto :output_option
if /I "%~1"=="--output" goto :output_option
if /I "%~1"=="--data-out" goto :assembler_value_option
if /I "%~1"=="--mif-out" goto :assembler_value_option
if /I "%~1"=="--data-mif-out" goto :assembler_value_option
if /I "%~1"=="--mif" (
    set "assembler_args=!assembler_args! --mif"
    shift
    goto :parse_args
)
if /I "%~1"=="--asm-out" goto :asm_output_option
if /I "%~1"=="--assembler" goto :assembler_option
if "%~1"=="--" (
    set "collect_mode=1"
    shift
    goto :collect_sources
)
set "current_arg=%~1"
if "!current_arg:~0,1!"=="-" (
    set "error_message=Unknown option: %~1"
    goto :fail
)
goto :add_source

:output_option
if "%~2"=="" (
    set "error_message=%~1 requires a value"
    goto :fail
)
set "output=%~2"
shift
shift
goto :parse_args

:assembler_value_option
if "%~2"=="" (
    set "error_message=%~1 requires a value"
    goto :fail
)
set "assembler_args=!assembler_args! %~1 "%~2""
shift
shift
goto :parse_args

:asm_output_option
if "%~2"=="" (
    set "error_message=--asm-out requires a value"
    goto :fail
)
set "asm_output=%~2"
shift
shift
goto :parse_args

:assembler_option
if "%~2"=="" (
    set "error_message=--assembler requires a value"
    goto :fail
)
set "assembler=%~2"
shift
shift
goto :parse_args

:collect_sources
if "%~1"=="" goto :args_done

:add_source
for %%I in ("%~1") do set "source[%source_count%]=%%~fI"
if not exist "!source[%source_count%]!" (
    set "error_message=Input file not found: !source[%source_count%]!"
    goto :fail
)
set /a source_count+=1
shift
if "%~1"=="" goto :args_done
if defined collect_mode goto :collect_sources
goto :parse_args

:args_done
if !source_count! EQU 0 (
    set "error_message=At least one C source is required"
    goto :fail
)

if not defined output for %%I in ("!source[0]!") do set "output=%%~dpnI.txt"
for %%I in ("!output!") do set "output=%%~fI"
if defined asm_output for %%I in ("!asm_output!") do set "asm_output=%%~fI"

set "project_dir=!script_dir!"
if not defined assembler if defined ASSEMBLER_EX_ISA set "assembler=%ASSEMBLER_EX_ISA%"
if not defined assembler if exist "!project_dir!\assembler-EX_ISA.exe" set "assembler=!project_dir!\assembler-EX_ISA.exe"
if not defined assembler if exist "!project_dir!\assembler-EX_ISA" set "assembler=!project_dir!\assembler-EX_ISA"
if not defined assembler (
    set "error_message=EX-ISA assembler not found; use --assembler FILE or set ASSEMBLER_EX_ISA"
    goto :fail
)

set "tmpdir=%TEMP%\8cc-ex-isa.!RANDOM!!RANDOM!"
md "!tmpdir!" >nul 2>&1
if errorlevel 1 (
    set "error_message=Could not create temporary directory: !tmpdir!"
    goto :fail
)
set "merged=!tmpdir!\merged.s"
type nul > "!merged!"

pushd "!script_dir!"
if exist "!project_dir!\8cc.exe" (set "compiler=!project_dir!\8cc.exe") else if exist "!project_dir!\8cc" (set "compiler=!project_dir!\8cc") else (
    popd
    set "error_message=8cc compiler not found in project root; move the working executable from build\ first"
    goto :fail
)
set /a last_source=source_count-1

for /L %%N in (0,1,!last_source!) do (
    set "source=!source[%%N]!"
    for %%I in ("!source!") do set "stem=%%~nI"
    set "STEM=!stem!"
    for /f "delims=" %%S in ('powershell -NoProfile -Command "$env:STEM -replace '[^A-Za-z0-9_]', '_'"') do set "stem=%%S"
    set "module_id=m%%N_!stem!"
    set "asm=!tmpdir!\module-%%N.s"
    for %%I in ("!source!") do set "source_dir=%%~dpI"
    set "source_dir=!source_dir:~0,-1!"
    >>"!merged!" echo # EX-ISA module !module_id!: !source!
    "!compiler!" -I"!source_dir!" -I"!project_dir!\include" -mex-isa -S --module-id "!module_id!" -o "!asm!" "!source!"
    if errorlevel 1 (
        popd
        set "error_message=Compiler failed for !source!"
        goto :fail
    )
    type "!asm!" >> "!merged!"
    >>"!merged!" echo.
    >>"!merged!" echo # End EX-ISA module !module_id!
)
popd

if defined asm_output copy /Y "!merged!" "!asm_output!" >nul
if errorlevel 1 (
    set "error_message=Could not write assembly output: !asm_output!"
    goto :fail
)
"!assembler!" "!merged!" "!output!" !assembler_args!
if errorlevel 1 (
    set "error_message=EX-ISA assembler failed"
    goto :fail
)
echo Linked !source_count! EX-ISA source module^(s^) into !output!
goto :cleanup_success

:usage_error
echo Usage: %~nx0 [options] source.c [source.c ...] 1>&2
echo Use "%~nx0 --help" for options. 1>&2
exit /b 1

:help
echo Usage: %~nx0 [options] source.c [source.c ...]
echo.
echo Options:
echo   -o, --output FILE       instruction output (default: first source .txt)
echo   --data-out FILE         data output
echo   --mif                   also write instruction and data MIF files
echo   --mif-out FILE          instruction MIF output
echo   --data-mif-out FILE     data MIF output
echo   --asm-out FILE          keep the merged assembly source
echo   --assembler FILE        EX-ISA assembler executable
echo   -h, --help              show this help
exit /b 0

:fail
echo !error_message! 1>&2
if defined tmpdir rd /s /q "!tmpdir!" >nul 2>&1
exit /b 1

:cleanup_success
if defined tmpdir rd /s /q "!tmpdir!" >nul 2>&1
exit /b 0