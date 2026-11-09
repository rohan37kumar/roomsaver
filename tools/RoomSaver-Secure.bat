@echo off
rem ===========================================================================
rem  RoomSaver — secure screensaver launcher
rem  Shows the room scene fullscreen. As soon as you move the mouse or press a
rem  key to dismiss it, the PC LOCKS (Windows login required to return).
rem
rem  Usage: put a SHORTCUT to this .bat on your Desktop, then in the shortcut's
rem  Properties set a "Shortcut key" (e.g. Ctrl+Alt+L). Keep this .bat inside the
rem  RoomSaver folder (next to RoomSaver.scr) — %~dp0 points at that folder.
rem ===========================================================================
start "" "%~dp0RoomSaver.scr" /s /lock
