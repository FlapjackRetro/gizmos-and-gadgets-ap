@echo off
rem Double-click to put the Archipelago logo on the title screen (builds GIZMOAP1.DAT in your game folder).
rem Runs title-logo.ps1 without changing the system's PowerShell execution policy. Arguments are passed
rem through, e.g.  title-logo.bat -GameDir C:\DOS\Gizmos
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0title-logo.ps1" %*
echo.
pause
