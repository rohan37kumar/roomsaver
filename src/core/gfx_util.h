#pragma once

// Reads the current GL back buffer and writes a PNG (row-flipped to image space).
// Used by --selftest and for capturing frames during development.
bool save_screenshot(const wchar_t* path, int width, int height);
