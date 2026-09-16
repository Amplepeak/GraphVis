@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

:: ============================================================
::  GraphVis - remove the scratch that has built up at the root
:: ============================================================
::
:: What this deletes is LISTED BEFORE IT ASKS, and it asks. Nothing here is
:: part of the build, and nothing here is the only copy of anything - every
:: item was checked against the repository first.
::
:: What it deliberately does NOT touch, because it was checked and is live:
::
::   install.bat            BUILD-AND-CHECK.bat calls it. Deleting it breaks
::                          the build outright.
::   build.bat              install.bat + a release build, with a pause.
::                          BUILD-AND-CHECK exists precisely because this one
::                          pauses; it is the one to run by hand.
::   BUILD-AND-CHECK.bat    the main build.
::   CHECK-GRAPHS.bat       the engine sweep.
::   MAKE-GALLERY.bat       draws all 434 engines.
::   BACKUP.bat             backups.
::   COMMIT.bat             commits, and reads commit-message.txt.
::   GIT-STATUS.bat         status.
::   CLEAN-SPACE.bat        disk cleanup. Useful - this project has filled a
::                          disk before.
::   COLLECT-CRASH-REPORT   writes crash-report.txt when the app dies.
::   WATCH-BUILD.bat        rebuilds on file change.
::   diagnose.bat           }  thin wrappers over live PowerShell in tools\.
::   verify-build.bat       }  Deleting the .bat orphans the .ps1.
::   BUILD-AND-DIAGNOSE.bat }
::   START_HERE.txt         the instructions for someone opening the folder
::                          for the first time. Not scratch.
::   install.sh run.sh build.sh   the Linux path. Untested, not dead.
::
:: In short: the .bat files at the root are almost all load-bearing. The
:: scratch is the logs and the "Claude outputs" folder.

echo ================================================================
echo   GraphVis cleanup
echo ================================================================
echo.
echo This will delete:
echo.

set "FOUND=0"

call :offer "full-log.txt"        "build diagnosis output - BUILD-AND-DIAGNOSE.bat rewrites it"
call :offer "startup-stage.txt"   "a staging note from 7 September"
call :offer "diagnose-log.txt"    "diagnose.bat output - rewritten each run"
call :offer "verify-log.txt"      "verify-build.bat output - rewritten each run"
call :offer "crash-report.txt"    "COLLECT-CRASH-REPORT.bat output - rewritten each run"

if exist "Claude outputs\" (
    set /a FOUND+=1
    echo   [folder] Claude outputs\
    for /f %%C in ('dir /b /a-d "Claude outputs" 2^>nul ^| find /c /v ""') do (
        echo            %%C files - screenshots, and STALE COPIES of source.
    )
    echo            It holds a QtPlotBackend.cpp of 1.4 MB. The real one is
    echo            626 KB, because it was split into six engine groups on
    echo            14 September. A file of that name, that size, next to the
    echo            real tree is worth deleting on its own.
    echo            latex.py, operations.py, test_fuzz.py and
    echo            science-tests.yml in there were each checked and all four
    echo            are present in the repository.
)

echo.
if "%FOUND%"=="0" (
    echo Nothing to remove - this folder is already clean.
    echo.
    pause
    exit /b 0
)

echo Nothing above is referenced by any build script.
echo.
set "ANSWER="
set /p "ANSWER=Type Y and press Enter to delete, anything else to cancel: "
if /i not "%ANSWER%"=="Y" (
    echo.
    echo Cancelled. Nothing was deleted.
    echo.
    pause
    exit /b 0
)

echo.
call :kill "full-log.txt"
call :kill "startup-stage.txt"
call :kill "diagnose-log.txt"
call :kill "verify-log.txt"
call :kill "crash-report.txt"

if exist "Claude outputs\" (
    rmdir /s /q "Claude outputs"
    if exist "Claude outputs\" (
        echo   [FAILED] Claude outputs\ - is a file in it open somewhere?
    ) else (
        echo   deleted  Claude outputs\
    )
)

echo.
echo Done.
echo.
pause
exit /b 0

:offer
if exist "%~1" (
    set /a FOUND+=1
    echo   %~1
    echo            %~2
)
exit /b 0

:kill
if exist "%~1" (
    del /q "%~1" 2>nul
    if exist "%~1" (echo   [FAILED] %~1 - open somewhere?) else (echo   deleted  %~1)
)
exit /b 0
