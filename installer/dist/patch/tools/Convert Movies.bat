@echo off
rem Convert Movies.bat: the double-click entry point for convert_movies.ps1.
rem
rem Two ways to use this:
rem   1. Double-click it directly - it asks for your game folder.
rem   2. Drag your game folder (the one WMAIN.EXE is in) onto this file - no typing needed at all.
rem
rem PowerShell scripts don't run on double-click by default (Windows opens them in a text editor
rem instead), and a plain "powershell -File" call closes its window the instant the script finishes,
rem before a non-technical user could ever read a success message or an error. This batch file exists
rem to fix both: %~dp0 finds convert_movies.ps1 next to this file regardless of where it was launched
rem from, -ExecutionPolicy Bypass lets it run without changing anything system-wide or permanent
rem (the bypass applies only to this one process), and the "pause" at the end holds the window open
rem until a key is pressed.

setlocal

rem MODIFIED FOR THE OPENPHANTOM INSTALLER. Not in the patch archive this file came from.
rem
rem The installer carries FFmpeg and puts it beside libVLC, so convert_movies.ps1 finds it on PATH
rem rather than downloading 106 MB on the first conversion. It looks in its own cache first and on
rem PATH second, and this is the second step.
rem
rem setlocal above keeps this to this window. Nothing on the machine is changed, and an FFmpeg the
rem player installed themselves still wins if it is already cached.
set "PATH=%~dp0..\mods\fmv;%PATH%"

rem A folder dragged onto a batch file can arrive with a trailing backslash, and a backslash sitting
rem directly in front of the closing quote of -GameDirectory "..." further down is an ESCAPE by the
rem time PowerShell parses the command line - so the quote is swallowed and PowerShell is handed one
rem mangled argument instead of a path. The blank check below still sees a value in that case, so the
rem interactive fallback that exists for exactly this does not run either, and the user just gets a
rem confusing failure.
rem
rem An ordinary path loses the backslash, which changes nothing about which folder is meant. A drive
rem root cannot: "C:" is the CURRENT directory on drive C, not its root, so stripping there would
rem quietly point at a different place. It gets a second backslash instead - "C:\\" - because the
rem escape consumes exactly one and what reaches PowerShell is "C:\" again. Leaving the root alone
rem was the earlier version here and it put back the very mangling the strip exists to remove.
rem
rem The jumps are what make this safe when there is no argument at all. Written as one line of
rem chained ifs, cmd substitutes the empty GAMEDIR before it evaluates the "defined" test meant to
rem guard the rest, and the comparison of two empty strings that this leaves behind is refused as a
rem syntax error - the line never runs, it fails to parse. Jumping past those lines instead means
rem they are never parsed at all.
set "GAMEDIR=%~1"
if not defined GAMEDIR goto :launch
if not "%GAMEDIR:~-1%"=="\" goto :launch
if "%GAMEDIR:~-2%"==":\" goto :root
set "GAMEDIR=%GAMEDIR:~0,-1%"
goto :launch

:root
set "GAMEDIR=%GAMEDIR%\"

:launch
rem -Interactive is passed on both paths below: dragging a folder here already answers the game
rem folder question, but it must not also silently skip the "what size do you want" question -
rem convert_movies.ps1 treats those as two separate prompts for exactly that reason.
if not defined GAMEDIR (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_movies.ps1" -Interactive
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_movies.ps1" -GameDirectory "%GAMEDIR%" -Interactive
)

echo.
pause
