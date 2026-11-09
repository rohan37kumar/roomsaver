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
rem  RoomSaver locks the PC itself when it is dismissed. This script also waits
rem  for it to exit and then locks again, so the PC still ends up locked if
rem  RoomSaver crashes or is closed some other way. (Locking a PC that is
rem  already locked does nothing.)
rem ===========================================================================
"%~dp0RoomSaver.exe" /s /lock
"%SystemRoot%\System32\rundll32.exe" user32.dll,LockWorkStation
