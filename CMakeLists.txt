cmake_minimum_required(VERSION 3.20)
project(RoomSaver LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "RoomSaver is a Windows screensaver (Win32/WGL/WinRT/WASAPI) and only builds on Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(TP ${CMAKE_SOURCE_DIR}/third_party)

add_executable(RoomSaver WIN32
    src/main.cpp
    src/app.cpp
    src/core/platform.cpp
    src/core/log.cpp
    src/core/gfx_util.cpp
    src/core/shader.cpp
    src/core/model.cpp
    src/core/postfx.cpp
    src/core/textrender.cpp
    src/core/nowplaying.cpp
    src/core/audiobeat.cpp
    src/core/settings.cpp
    src/core/config.cpp
    src/core/roomsaver.rc
    ${TP}/glad/src/gl.c
    ${TP}/stb/stb_impl.c
    ${TP}/cgltf/cgltf_impl.c
    ${TP}/kissfft/kiss_fft.c
    ${TP}/kissfft/kiss_fftr.c
)

target_include_directories(RoomSaver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${TP}/glad/include
    ${TP}/glm
    ${TP}/stb
    ${TP}/cgltf
    ${TP}/kissfft
)

# Native Windows: OpenGL + Win32 + C++/WinRT (SMTC via windowsapp) + WASAPI (ole32)
# + config dialog (comctl32 trackbars, comdlg32 file picker) + registry (advapi32).
target_link_libraries(RoomSaver PRIVATE
    opengl32 gdi32 user32 shell32 windowsapp ole32 comctl32 comdlg32 advapi32)

if(MSVC)
    target_compile_options(RoomSaver PRIVATE /W3 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_definitions(RoomSaver PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN NOMINMAX
        STBI_WINDOWS_UTF8            # stb_image: accept UTF-8 picture paths
        _CRT_SECURE_NO_WARNINGS)
endif()

# Dev fallback: if assets aren't found next to the exe, load them from the source
# tree. A shipped .scr uses the copies placed beside it by the post-build step.
target_compile_definitions(RoomSaver PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

set_target_properties(RoomSaver PROPERTIES OUTPUT_NAME "RoomSaver")

# --- Packaging: stage a self-contained, shippable folder next to the exe -------
# Copy shaders/ + assets/ beside the binary and produce RoomSaver.scr (a .scr is
# just a renamed .exe). After a build, <build>/<config>/ is drop-in installable.
add_custom_command(TARGET RoomSaver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:RoomSaver>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets  $<TARGET_FILE_DIR:RoomSaver>/assets
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:RoomSaver> $<TARGET_FILE_DIR:RoomSaver>/RoomSaver.scr
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/tools/RoomSaver-Secure.bat $<TARGET_FILE_DIR:RoomSaver>/RoomSaver-Secure.bat
    VERBATIM)
