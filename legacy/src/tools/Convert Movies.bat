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

rem -Interactive is passed on both paths below: dragging a folder here already answers the game
rem folder question, but it must not also silently skip the "what size do you want" question -
rem convert_movies.ps1 treats those as two separate prompts for exactly that reason.
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_movies.ps1" -Interactive
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0convert_movies.ps1" -GameDirectory "%~1" -Interactive
)

echo.
pause
