@echo off
REM  run-vnovel.cmd - launch a KiriKiri game with the vnovel core.
REM
REM  The core is ONE self-contained DLL. Its only imports are Windows' own
REM  (freetype, vorbis, ogg, bzip2, brotli and SDL2 are linked in), so nothing
REM  else has to sit beside it.
REM
REM  The engine needs a small set of NON-COMPILED assets at runtime: the TJS KAG
REM  parser, plugin shims, the plugin override table, and a fallback font. They
REM  are data, so they belong in a per-core subdirectory of the frontend's system
REM  ("BIOS") directory:
REM
REM      Recalbox / Pi     /recalbox/share/bios/vnovel
REM      Windows (share)   K:\Recalbox\bios\vnovel
REM      elsewhere         <system_directory>\vnovel
REM
REM  containing overrides.txt, startup.tjs, mona.ttf and system_polyfill\*.tjs.
REM  The core finds it itself, so normally nothing has to be set here; see the
REM  "engine data" block below for the fallback when there is no BIOS copy.
REM
REM  Usage:  run-vnovel.cmd <game-dir-or-archive> [extra retroarch args]
REM          run-vnovel.cmd "K:\Software\Fate-Stay-Night-engpatch"
REM
REM  Everything the run prints is also written to vnovel.log beside this script,
REM  so a launch that fails can be read after the console closes.

setlocal enableextensions

set "COREDIR=%~dp0"
if "%COREDIR:~-1%"=="\" set "COREDIR=%COREDIR:~0,-1%"
set "RETROARCH=%RETROARCH%"
if "%RETROARCH%"=="" set "RETROARCH=C:\RetroArch-Win64\retroarch.exe"
set "LOG=%COREDIR%vnovel.log"

set "GAME=%~1"
if "%GAME%"=="" (
  echo ERROR: no game given.
  echo     run-vnovel.cmd "K:\Software\Fate-Stay-Night-engpatch"
  exit /b 1
)

> "%LOG%" echo === vnovel launch  %DATE% %TIME% ===

REM preflight
if not exist "%COREDIR%\vnovel_libretro.dll" (
  echo ERROR: vnovel_libretro.dll missing next to this script.
  >>"%LOG%" echo ERROR: vnovel_libretro.dll missing.
  goto :fail
)
if not exist "%GAME%" (
  echo ERROR: game not found: "%GAME%"
  >>"%LOG%" echo ERROR: game not found: "%GAME%"
  goto :fail
)

REM engine data
REM The core resolves this itself: at startup it asks the frontend for its
REM system ("BIOS") directory and, if <system dir>\vnovel\system_polyfill\
REM exists, loads the polyfill from there. So the normal setup needs nothing
REM here — put the data in the BIOS directory once (tools/deploy-engine-data.sh
REM does it on a Unix box) and every launch finds it.
REM
REM A self-contained unpack with no BIOS copy can point at its own instead.
REM First hit wins:
REM
REM   1. an explicit KRKRSDL2_DATA_PATH
REM   2. krkrsdl2-data\ beside this script
REM   3. whatever the core resolves (do nothing)
if not defined KRKRSDL2_DATA_PATH (
  if exist "%COREDIR%\krkrsdl2-data\overrides.txt" (
    set "KRKRSDL2_DATA_PATH=%COREDIR%\krkrsdl2-data"
  )
)

REM If we did point it at a local copy, it must be complete. Starting with a
REM partial one is worse than not starting: the scenario parser or the override
REM table goes missing, and the failure surfaces much later as an unrelated
REM error. When KRKRSDL2_DATA_PATH is unset the core decides, so nothing to
REM check here.
if defined KRKRSDL2_DATA_PATH (
  if not exist "%KRKRSDL2_DATA_PATH%\system_polyfill\PluginStubs.tjs" (
    echo ERROR: engine data incomplete: no system_polyfill\ in
    echo        "%KRKRSDL2_DATA_PATH%"
    >>"%LOG%" echo ERROR: no system_polyfill in "%KRKRSDL2_DATA_PATH%"
    goto :fail
  )
)

REM diagnostics
REM VNOVEL_DEBUG is one 64-bit mask, 8 bits per module (16 for krkr); the
REM engine-side names are in deps/krkrsdl2/src/core/base/sdl2/DebugFlags.h and
REM the core's in src/core_debug.h. Names may be qualified by module.
REM
REM   VNOVEL_DEBUG=krkr:script,core:input   tags, and input as the engine sees it
REM   VNOVEL_DEBUG=krkr:fill                every rect filled, with its face
REM   VNOVEL_DEBUG=krkr:draw                every composited draw's branch
REM
REM The per-glyph / per-rect probes bury everything else on a busy screen, so
REM they are off by default. The engine's own log needs no switch - it always
REM goes to stderr.
set "VNOVEL_DEBUG=krkr:script,core:input"

REM module directory on PATH
REM Windows resolves a DLL's imports via the *executable's* directory and PATH,
REM not the DLL's own. Prepend rather than replace, so the user's PATH survives.
set "PATH=%COREDIR%;%PATH%"

>>"%LOG%" echo game      : %GAME%
>>"%LOG%" echo core      : %COREDIR%\vnovel_libretro.dll
>>"%LOG%" echo data path : %KRKRSDL2_DATA_PATH%

echo Game      : %GAME%
echo Core      : %COREDIR%\vnovel_libretro.dll
if defined KRKRSDL2_DATA_PATH (
  echo Data path : %KRKRSDL2_DATA_PATH%  ^(from this script^)
) else (
  echo Data path : resolved by the core -
  echo             ^<system dir^>\vnovel, else the installed data dir
)
echo Log       : %LOG%
echo.
echo Launching RetroArch; output is also saved to the log above.
echo.

REM Loading data.xp3 (not a scenario file) is what makes the core select the
REM KiriKiri engine; it then finds the scenario archive inside. The English
REM patches (patch.xp3, patch2.xp3, ...) are stacked by the game's own
REM Initialize.tjs, not by the core.
REM
REM Plain cmd redirection, NOT PowerShell. PowerShell 5.1 turns every stderr
REM line from a native command into an ErrorRecord ("NativeCommandError"), and
REM with a piped Tee-Object that aborted the whole run partway through plugin
REM loading — the log ended mid-stream and RetroArch exited without ever
REM reaching the startup script. The engine writes a lot to stderr by design,
REM so this must be a dumb byte redirect.
"%RETROARCH%" -L "%COREDIR%\vnovel_libretro.dll" "%GAME%" %* >>"%LOG%" 2>&1

set "RC=%ERRORLEVEL%"
>>"%LOG%" echo === exit %RC% ===

echo.
echo --- %LOG% ---
type "%LOG%"
echo --- end of log ---
echo.
echo RetroArch exited with code %RC%.
endlocal & exit /b %RC%

:fail
echo.
echo See %LOG%
endlocal & exit /b 1
