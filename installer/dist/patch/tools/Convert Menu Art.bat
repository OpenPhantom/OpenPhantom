@echo off
rem Convert Menu Art.bat: the double-click entry point for convert_menu.ps1.
rem
rem Two ways to use this:
rem   1. Double-click it directly - it asks for your game folder, your screen size, and how the
rem      4:3 artwork should fill a widescreen display.
rem   2. Drag your game folder (the one WMAIN.EXE is in) onto this file - it still asks the other
rem      two questions, it just does not have to ask where the game is.
rem
rem PowerShell scripts don't run on double-click by default (Windows opens them in a text editor
rem instead), and a plain "powershell -File" call closes its window the instant the script finishes,
rem before a non-technical user could ever read a success message or an error. This batch file exists
rem to fix both: %~dp0 finds convert_menu.ps1 next to this file regardless of where it was launched
rem from, -ExecutionPolicy Bypass lets it run without changing anything system-wide or permanent
rem (the bypass applies only to this one process), and the "pause" at the end holds the window open
rem until a key is pressed.
rem
rem To undo a conversion, run this with -Remove, or just delete the menu_hd folder it wrote.

setlocal

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
rem escape consumes exactly one and what reaches PowerShell is "C:\" again.
rem
rem The jumps are what make this safe when there is no argument at all. Written as one line of
rem chained ifs, cmd substitutes the empty GAMEDIR before it evaluates the "defined" test meant to
rem guard the rest, and the comparison of two empty strings that this leaves behind is refused as a
rem syntax error - the line never runs, it fails to parse. Jumping past those lines instead means
rem they are never parsed at all.
set "GAMEDIR=%~1"
if not defined GAMEDIR goto :launch
if "%GAMEDIR%"=="-Remove" goto :launch
if not "%GAMEDIR:~-1%"=="\" goto :launch
if "%GAMEDIR:~-2%"==":\" goto :root
set "GAMEDIR=%GAMEDIR:~0,-1%"
goto :launch

:root
set "GAMEDIR=%GAMEDIR%\"

:launch
rem -Remove passed straight through, so "Convert Menu Art.bat -Remove" undoes a conversion. It still
rem needs to know which game folder, so it asks, exactly as a plain double-click does.
if "%~1"=="-Remove" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_menu.ps1" -Remove
    goto :done
)

rem -Interactive is passed on both paths below: dragging a folder here already answers the game
rem folder question, but it must not also silently skip the screen size and fit questions -
rem convert_menu.ps1 treats those as separate prompts for exactly that reason.
if not defined GAMEDIR (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_menu.ps1" -Interactive
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_menu.ps1" -GameDirectory "%GAMEDIR%" -Interactive
)

:done
echo.
pause
