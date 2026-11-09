#pragma once

// Single source of truth for the version (used by roomsaver.rc and the log).
// Bump these for a new version; the strings below follow automatically.
#define ROOMSAVER_VERSION_MAJOR 1
#define ROOMSAVER_VERSION_MINOR 1
#define ROOMSAVER_VERSION_PATCH 0

#define ROOMSAVER_STR_(x) #x
#define ROOMSAVER_STR(x)  ROOMSAVER_STR_(x)

#define ROOMSAVER_VERSION_STRING  ROOMSAVER_STR(ROOMSAVER_VERSION_MAJOR) "." \
                                  ROOMSAVER_STR(ROOMSAVER_VERSION_MINOR) "." \
                                  ROOMSAVER_STR(ROOMSAVER_VERSION_PATCH)

#define ROOMSAVER_AUTHOR     "kc (rohan37kumar)"
#define ROOMSAVER_COPYRIGHT  "Copyright (c) 2026 " ROOMSAVER_AUTHOR
#define ROOMSAVER_URL        "https://github.com/rohan37kumar"
