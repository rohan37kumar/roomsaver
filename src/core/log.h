#pragma once

// Lightweight logging: writes to %TEMP%\RoomSaver.log and OutputDebugString.
// A GUI-subsystem app has no console, so a log file is our main diagnostic.

void log_init();
void log_msg(const char* fmt, ...);

#define LOGI(...) log_msg(__VA_ARGS__)
