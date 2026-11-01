@echo off
rem install.bat - install the VNovel libretro core into a RetroArch (or
rem RetroArch-based frontend) cores directory on Windows.
rem
rem   install.bat                      detect the frontend's core directory
rem   install.bat --dir "C:\path\cores"
rem   install.bat --uninstall
rem
rem The Windows counterpart of install.sh. The core is one self-contained DLL
rem (the runtime and both engines are linked in), so this installs the single
rem .dll. The database and the core-info file go to RetroArch's own directories
rem so the frontend can name games on a scan, and the KiriKiri engine data goes
rem to the frontend's system directory as vnovel\.
rem
rem Two bundle shapes are understood:
rem   flat      the core, the .info, vnovel.rdb, vnovel\ and this script side
rem             by side
rem   drop-in   cores\, system\ and database\rdb\ at the root, so extracting the
rem             archive into a portable RetroArch directory is the whole install
rem
rem Plain cmd.exe; no PowerShell, no WSL, no external tools.

setlocal EnableExtensions DisableDelayedExpansion

set "PROG=%~nx0"
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

rem Where the bundle's own files are. The drop-in archive keeps the core under
rem cores\, the database under database\rdb\ and the engine data under system\;
rem the flat bundle keeps them beside this script.
set "SRC_CORES=%HERE%"
set "SRC_DB=%HERE%"
set "SRC_SYSTEM=%HERE%"
if exist "%HERE%\cores\vnovel_libretro.dll" set "SRC_CORES=%HERE%\cores"
if exist "%HERE%\database\rdb\vnovel.rdb" set "SRC_DB=%HERE%\database\rdb"
if exist "%HERE%\system\vnovel\overrides.txt" set "SRC_SYSTEM=%HERE%\system"

rem The single artifact this release ships. A bundle carries exactly one, under
rem the platform's own extension; the first that exists is the one installed.
set "CORE="
for %%E in (dll so dylib) do if not defined CORE if exist "%SRC_CORES%\vnovel_libretro.%%E" set "CORE=vnovel_libretro.%%E"
set "INFO=vnovel_libretro.info"
set "RDB=vnovel.rdb"

set "CORES_DIR="
set "SYSTEM_DIR="
set "UNINSTALL=0"
set "DRY=0"
set "RC=0"

:parse
if "%~1"=="" goto :parsed
if /i "%~1"=="--dir" goto :arg_dir
if /i "%~1"=="--system-dir" goto :arg_system_dir
if /i "%~1"=="--uninstall" goto :arg_uninstall
if /i "%~1"=="--dry-run" goto :arg_dry
if /i "%~1"=="--prefix" goto :arg_prefix
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="/?" goto :usage
echo %PROG%: unknown option: %~1 (try --help)>&2
set "RC=1"
goto :finish

:arg_dir
if "%~2"=="" goto :arg_dir_missing
set "CORES_DIR=%~2"
shift
shift
goto :parse

:arg_dir_missing
echo %PROG%: --dir needs an argument>&2
set "RC=1"
goto :finish

:arg_system_dir
if "%~2"=="" goto :arg_system_dir_missing
set "SYSTEM_DIR=%~2"
shift
shift
goto :parse

:arg_system_dir_missing
echo %PROG%: --system-dir needs an argument>&2
set "RC=1"
goto :finish

:arg_uninstall
set "UNINSTALL=1"
shift
goto :parse

:arg_dry
set "DRY=1"
shift
goto :parse

:arg_prefix
echo %PROG%: --prefix is not used on Windows; pass --dir "C:\path\to\cores">&2
set "RC=1"
goto :finish

:usage
echo Usage: %PROG% [options]
echo.
echo   --dir DIR         install the core into DIR (the frontend's cores directory)
echo   --system-dir DIR  the frontend's system directory; the engine data goes in
echo                     DIR\vnovel (default: derived from --dir)
echo   --uninstall       remove a previously installed core
echo   --dry-run         print what would be done, install nothing
echo   -h, --help        this help
echo.
echo With no --dir, the bundle's own cores\ directory is used when it has one
echo (the drop-in archive), then common RetroArch locations are probed.
goto :finish

:parsed

rem where the core goes

if defined CORES_DIR goto :normalize_cores

rem The drop-in archive: cores\ at the root is the cores directory itself.
if exist "%HERE%\cores\vnovel_libretro.dll" set "CORES_DIR=%HERE%\cores"
if defined CORES_DIR goto :normalize_cores

rem A portable RetroArch keeps cores\ beside retroarch.exe; if this bundle was
rem unpacked there, that is the cores directory.
if exist "%HERE%\retroarch.exe" if exist "%HERE%\cores" set "CORES_DIR=%HERE%\cores"
if defined CORES_DIR goto :normalize_cores

rem The ordinary Windows install: %APPDATA%\RetroArch\cores.
if defined APPDATA if exist "%APPDATA%\RetroArch\cores" set "CORES_DIR=%APPDATA%\RetroArch\cores"
if defined CORES_DIR goto :normalize_cores
if defined USERPROFILE if exist "%USERPROFILE%\AppData\Roaming\RetroArch\cores" set "CORES_DIR=%USERPROFILE%\AppData\Roaming\RetroArch\cores"
if defined CORES_DIR goto :normalize_cores

echo %PROG%: no cores directory found; pass --dir>&2
set "RC=1"
goto :finish

:normalize_cores
rem Absolute, and without a trailing backslash. %%~fA resolves a relative path
rem and drops the trailing separator; the appended \. makes it drop one that was
rem already there, which %%~fA alone does not do on every cmd. A drive root
rem keeps its own, and Windows tolerates the doubled separator that then appears
rem in the paths below.
for %%A in ("%CORES_DIR%\.") do set "CORES_DIR=%%~fA"

rem The database lives beside the frontend config, not beside the cores.
set "DB_DIR="
rem The drop-in archive already carries database\rdb\ beside cores\.
if exist "%HERE%\database\rdb\vnovel.rdb" if /i "%CORES_DIR%"=="%HERE%\cores" set "DB_DIR=%HERE%\database\rdb"
if defined DB_DIR goto :no_db
rem Otherwise derive it when the cores sit directly under a RetroArch config
rem directory, which is the usual <config>\RetroArch\cores shape. retroarch.cfg
rem is the marker; database\ is accepted too, for a config not yet written.
if exist "%CORES_DIR%\..\retroarch.cfg" goto :have_db
if exist "%CORES_DIR%\..\database" goto :have_db
goto :no_db
:have_db
for %%A in ("%CORES_DIR%\..\database\rdb") do set "DB_DIR=%%~fA"
:no_db

rem The engine data lives in the frontend's system directory, as vnovel\.
rem The drop-in archive already carries system\ beside cores\.
if not defined SYSTEM_DIR if exist "%HERE%\system\vnovel\overrides.txt" if /i "%CORES_DIR%"=="%HERE%\cores" set "SYSTEM_DIR=%HERE%\system"
rem Otherwise derive it from the cores directory, when that sits under a
rem RetroArch config directory.
if not defined SYSTEM_DIR if exist "%CORES_DIR%\..\retroarch.cfg" for %%A in ("%CORES_DIR%\..\system") do set "SYSTEM_DIR=%%~fA"
if not defined SYSTEM_DIR if exist "%CORES_DIR%\..\database" for %%A in ("%CORES_DIR%\..\system") do set "SYSTEM_DIR=%%~fA"
if defined SYSTEM_DIR for %%A in ("%SYSTEM_DIR%\.") do set "SYSTEM_DIR=%%~fA"

if not defined CORE goto :no_core
if not exist "%SRC_CORES%\%CORE%" goto :no_core

rem uninstall

if "%UNINSTALL%"=="1" goto :do_uninstall

rem install

echo cores directory: %CORES_DIR%
if "%DRY%"=="1" goto :skip_mkdir
if not exist "%CORES_DIR%" mkdir "%CORES_DIR%"
if errorlevel 1 goto :fail_mkdir
:skip_mkdir

call :install_one "%SRC_CORES%\%CORE%" "%CORES_DIR%"
if errorlevel 1 goto :fail_core

if not exist "%SRC_CORES%\%INFO%" goto :no_info
call :install_one "%SRC_CORES%\%INFO%" "%CORES_DIR%"
:no_info

if not exist "%SRC_DB%\%RDB%" goto :no_rdb
if not defined DB_DIR goto :rdb_note
if "%DRY%"=="1" goto :skip_db_mkdir
if not exist "%DB_DIR%" mkdir "%DB_DIR%"
if errorlevel 1 goto :fail_db_mkdir
:skip_db_mkdir
call :install_one "%SRC_DB%\%RDB%" "%DB_DIR%"
goto :no_rdb
:rdb_note
echo note: install %RDB% into your frontend's database\rdb directory to get game names
:no_rdb

rem The KiriKiri engine's data, as vnovel\ in the frontend's system directory.
rem Not optional: without system_polyfill\ the engine has no scenario parser and
rem no .xp3 game can boot, and without overrides.txt it tries to load the game's
rem own Windows plugins and aborts startup.
rem
rem Merged rather than replaced: a reader may have dropped in a font of their
rem own under one of the shipped names, and install.sh merges for the same
rem reason.
if not exist "%SRC_SYSTEM%\vnovel" goto :no_engdata
if not defined SYSTEM_DIR goto :no_system_dir
echo install vnovel\ (engine data) -^> %SYSTEM_DIR%\
if "%DRY%"=="1" goto :after_engdata
if not exist "%SYSTEM_DIR%" mkdir "%SYSTEM_DIR%"
if errorlevel 1 goto :fail_system_mkdir
call :install_tree "%SRC_SYSTEM%\vnovel" "%SYSTEM_DIR%\vnovel"
if errorlevel 1 goto :fail_engdata
rem Load-bearing and silently fatal when missing: the parser is the whole
rem polyfill, and the override table is what stops the engine from trying to
rem run the game's own PE plugins.
if not exist "%SYSTEM_DIR%\vnovel\overrides.txt" goto :fail_engdata
if not exist "%SYSTEM_DIR%\vnovel\system_polyfill\PluginStubs.tjs" goto :fail_engdata
if not exist "%SYSTEM_DIR%\vnovel\system_polyfill\KAGParser.tjs" goto :fail_engdata
:after_engdata

echo done. Load the 'VNovel' core; select a game's nscript.dat, pscript.dat or data.xp3.
goto :finish

:do_uninstall
set "TARGET=%CORES_DIR%\%CORE%"
if not exist "%TARGET%" goto :uninstall_db
call :remove "%TARGET%"
:uninstall_db
if not defined DB_DIR goto :uninstall_done
if not exist "%DB_DIR%\%RDB%" goto :uninstall_done
call :remove "%DB_DIR%\%RDB%"
:uninstall_done
echo done
goto :finish

:no_core
echo %PROG%: no vnovel_libretro.dll in the bundle; run this from the unpacked release>&2
set "RC=1"
goto :finish

:fail_mkdir
echo %PROG%: cannot create %CORES_DIR%>&2
set "RC=1"
goto :finish

:fail_db_mkdir
echo %PROG%: cannot create %DB_DIR%>&2
set "RC=1"
goto :finish

:fail_system_mkdir
echo %PROG%: cannot create %SYSTEM_DIR%>&2
set "RC=1"
goto :finish

:no_system_dir
echo %PROG%: cannot work out the system directory from %CORES_DIR%; pass --system-dir>&2
set "RC=1"
goto :finish

:fail_core
echo %PROG%: failed to install %CORE%>&2
set "RC=1"
goto :finish

:fail_engdata
echo %PROG%: failed to install vnovel\ (engine data); KiriKiri games will not run>&2
set "RC=1"
goto :finish

:no_engdata
echo %PROG%: vnovel\ (engine data) missing from the bundle; KiriKiri games will not run>&2
set "RC=1"
goto :finish

:finish
rem Keep the window open when the script was double-clicked, so the result is
rem readable; a run from an existing prompt returns immediately.
echo "%cmdcmdline%" | find /i "%~nx0" >nul
if not errorlevel 1 pause
exit /b %RC%

rem --- subroutines ---------------------------------------------------------

:install_one
rem %1 = source file, %2 = destination directory
if not exist "%~1" exit /b 1
rem Nothing to do when the file is already in the destination directory (the
rem drop-in archive run with no --dir).
for %%A in ("%~1") do set "S1=%%~dpA"
for %%B in ("%~2") do set "S2=%%~fB\"
if /i "%S1%"=="%S2%" exit /b 0
echo install %~nx1 -^> %~2\
if "%DRY%"=="1" exit /b 0
rem Install to a temporary name and rename, so a partially-written core is
rem never loadable: the frontend may be running and will pick up the file on
rem its next scan.
set "TMPNAME=%~2\.%~nx1.new"
copy /y "%~1" "%TMPNAME%" >nul
if errorlevel 1 exit /b 1
move /y "%TMPNAME%" "%~2\%~nx1" >nul
if errorlevel 1 exit /b 1
exit /b 0

:install_tree
rem %1 = source directory, %2 = destination directory
for %%A in ("%~1") do set "S1=%%~fA"
for %%B in ("%~2") do set "S2=%%~fB"
if /i "%S1%"=="%S2%" exit /b 0
if "%DRY%"=="1" exit /b 0
xcopy /e /i /y "%~1" "%~2" >nul
rem xcopy reports 1 when there was nothing to copy, which is not an error.
if errorlevel 2 exit /b 1
exit /b 0

:remove
echo remove %~1
if "%DRY%"=="1" exit /b 0
del /f /q "%~1" >nul 2>&1
exit /b 0
