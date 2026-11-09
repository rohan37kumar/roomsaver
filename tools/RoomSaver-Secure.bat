@echo off
rem ===========================================================================
rem  RoomSaver - secure screensaver launcher
rem  Shows the room scene fullscreen. As soon as you move the mouse or press a
rem  key to dismiss it, the PC LOCKS (Windows login required to return).
rem
rem  Usage: put a SHORTCUT to this .bat on your Desktop, then in the shortcut's
rem  Properties set a "Shortcut key" (e.g. Ctrl+Alt+L) and "Run: Minimized".
rem  Keep this .bat inside the RoomSaver folder, next to RoomSaver.exe.
rem
rem  RoomSaver also locks the PC if it fails to start, crashes, loses focus to
rem  another window, or is closed some other way (a hidden helper process
rem  watches it), so this script just starts it and exits.
rem ===========================================================================
start "" "%~dp0RoomSaver.exe" /s /lock
