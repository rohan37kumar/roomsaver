# RoomSaver: Install & Usage

RoomSaver is a **screensaver**. One executable, three ways to reach it, all sharing
the same settings (stored in `HKCU\Software\RoomSaver`):

| Run as… | How |
|---|---|
| **Screensaver on a hotkey** (locks the PC on dismiss) | shortcut to `RoomSaver-Secure.bat` with a Shortcut key |
| **Screensaver when idle** (optionally locks) | Windows screen saver settings → **RoomSaver** |
| **Settings dialog** | `RoomSaver.exe /c` |

The scene (album art on the TV, album-colored glow + beat flicker, clock, framed
picture) is identical either way. RoomSaver never runs at login or in the background.

## Requirements

- **Windows 10 or 11** (x64).
- A GPU supporting **OpenGL 3.3** (any modern NVIDIA/AMD/Intel GPU).
- To build: **Visual Studio 2022+** with the *Desktop development with C++* workload
  (see the [README](../README.md#build)).
- To run on a PC without Visual Studio, you may need the **Microsoft Visual C++
  Redistributable (x64)**.

## What a build produces

After `cmake --build build --config Release`, the folder `build\Release\` is a
self-contained bundle:

```
build\Release\
├── RoomSaver.exe            # same binary: dev window, settings (/c)
├── RoomSaver.scr            # same binary, renamed: the screensaver
├── RoomSaver-Secure.bat     # launches the screensaver + locks on dismiss
├── shaders\                 # copied automatically (post-build)
└── assets\                  # room.glb, picture.jpg, placeholder (copied automatically)
```

The exe loads `shaders\` and `assets\` from beside itself, so always copy the
**whole** folder.

## Setup

### Step 1: Put the folder in a permanent home

Both the hotkey shortcut and the Windows screen-saver setting store the **full path**
to the binary, so pick a folder you won't move or delete, e.g.
`C:\Users\<you>\RoomSaver\`. Avoid `Downloads` and the `build\` folder.

Don't copy the lone `.scr` into `System32`: its assets wouldn't be there.

### Step 2: Desktop shortcut + hotkey (on demand, locks on dismiss)

`RoomSaver-Secure.bat` runs `RoomSaver.exe /s /lock`. It shows the scene fullscreen
and **locks the PC** the moment you dismiss it. If RoomSaver can't start, crashes,
loses keyboard focus to another window, or is closed some other way, it locks too
(a small hidden helper process watches it for the cases it can't catch itself).

1. Right-click `RoomSaver-Secure.bat` → **Show more options** → **Create shortcut**.
2. Drag that shortcut onto your **Desktop**.
3. Right-click the shortcut → **Properties**:
   - click the **Shortcut key** box and press your combo (e.g. `Ctrl`+`Alt`+`L`),
   - set **Run:** to **Minimized** so a console window doesn't flash,
   - **OK**.

The hotkey only works while the shortcut lives on the Desktop or in the Start Menu.
That's a Windows rule, not a RoomSaver one.

### Step 3: Idle screensaver (optional)

The easy way: right-click `RoomSaver.scr` → **Install**. The Windows *Screen Saver
Settings* dialog opens with RoomSaver selected. Set:

1. **Wait:** minutes of idle before it starts.
2. **On resume, display logon screen:** tick it to lock the PC on dismiss.
3. **OK**.

Or set it from a terminal (replace the path with your install folder):

```
reg add "HKCU\Control Panel\Desktop" /v SCRNSAVE.EXE        /t REG_SZ /d "C:\Users\<you>\RoomSaver\RoomSaver.scr" /f
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveActive    /t REG_SZ /d 1   /f
reg add "HKCU\Control Panel\Desktop" /v ScreenSaveTimeOut   /t REG_SZ /d 300 /f
reg add "HKCU\Control Panel\Desktop" /v ScreenSaverIsSecure /t REG_SZ /d 1   /f
```

(`ScreenSaveTimeOut` is in **seconds**, so 300 = 5 min. `ScreenSaverIsSecure` = lock
on resume.) Changes apply at the next idle period, with no reboot needed.

Windows' **Preview** button runs the saver via `/s`, and the small preview thumbnail
via `/p <hwnd>`. Both are supported.

### Step 4: Settings

Run `RoomSaver.exe /c` (or click **Settings…** in the Windows screen-saver dialog):

| Setting | Range | Effect |
|---|---|---|
| Framed picture | file path | Image shown in the wall frame (PNG/JPG/BMP) |
| TV text when nothing is playing | up to 24 characters | Wordmark on the idle TV. Default `kc`; empty = blank screen. Characters missing from the Consolas font show as boxes. |
| Show the room on | Main screen only / All screens / Chosen screens | Which screens show the room in fullscreen mode; every other screen goes black. See [Multiple screens](#multiple-screens). |
| Beat sensitivity | 0.20–3.00 | How strongly the bias lights flicker to the beat |
| Brightness | 0.50–2.00 | Overall scene brightness |

Values are saved to `HKCU\Software\RoomSaver` and picked up by every later launch.
To reset everything, delete that key.

## Multiple screens

In Settings, **Show the room on** picks where the room appears when the screensaver
runs (`/s`, the hotkey, or the idle screensaver):

- **Main screen only** (default): the room on your main display; others go black.
- **All screens**: a room on every connected screen, including ones plugged in later.
- **Chosen screens**: tick the screens you want in the list. Click **Identify** to
  flash each screen's number so you can tell identical monitors apart.

Details:

- Each screen gets its own full room, framed to that screen's shape (portrait
  monitors work too). Album art, colours, beat and clock stay in sync on all of them.
- Screens without the room are black. Mouse movement or a key press on **any** screen
  ends the screensaver.
- Chosen screens are remembered by the monitor itself, not by "Display 1/2", so
  they survive reboots and re-docking. A chosen screen that isn't connected is listed
  as *(not connected)* and skipped. If none of your chosen screens is connected, the
  room goes on the main screen so the saver is never all black.
- The small preview thumbnail in Windows' screen saver dialog always shows one room.

## The framed picture

Pick any image in Settings (**Browse…**), or drop a file named
`picture.png/.jpg/.jpeg/.bmp` into `assets\`. Any aspect ratio works: it's centered
and cover-fit to the frame (no stretching; edges may be cropped). With no picture at
all, a procedural dusk scene is shown.

## Command-line reference

`RoomSaver.exe` and `RoomSaver.scr` are the same binary and accept:

| Flag | Effect |
|---|---|
| *(no args)* or `/w` | windowed mode |
| `/s` | fullscreen screensaver; exits on mouse move / keypress / click |
| `/s /lock` | same, and **locks the workstation** on dismissal (what the `.bat` uses) |
| `/p <hwnd>` | render into the screen-saver dialog's preview thumbnail |
| `/c` | settings dialog |
| `--selftest=out.png` | render a frame in a 1280×720 window, save a screenshot, exit |

Developer environment variables:

| Variable | Effect |
|---|---|
| `ROOMSAVER_SELFTEST_DELAY_MS=<ms>` | keep rendering this long before the `--selftest` capture (lets album art arrive) |
| `ROOMSAVER_TV_IDLE=1` | force the idle TV screen even while music plays |
| `ROOMSAVER_BEAT_SENS=<x>` | override beat sensitivity |
| `ROOMSAVER_BEAT_LOG=1` | log beat level / intensity ~2×/s |
| `ROOMSAVER_PICROT=<deg>` | override the picture's UV rotation (default 90) |
| `ROOMSAVER_DUMP=1` | log every emissive node with its bounds |
| `ROOMSAVER_TEST_LAYOUT=x,y,w,h;...` | draw the room only into these rectangles of the window (client pixels, top-left origin), to test multi-screen layouts with `--selftest` on one monitor |

## Troubleshooting

Diagnostics go to **`%TEMP%\RoomSaver.log`** (the app has no console). A healthy run
logs `GL_RENDERER`, `PostFX::init <w>x<h>`, `model: loaded ... room.glb`,
`NowPlaying: SMTC session manager ready` and `AudioBeat: loopback started`. In
fullscreen mode it also lists every screen (`Screen N: ...`), marking the ones that
show the room with `[room]`.

- **Black screen / nothing renders:** make sure `shaders\` and `assets\` sit next to
  the `.exe`/`.scr`, then check the log for `model: loaded`.
- **TV shows the idle text:** nothing is playing, or the player doesn't publish a
  thumbnail to Windows' media controls. Art is retried for a few seconds after a track
  change.
- **No beat flicker:** the lights react to whatever plays on the **default output
  device**. Capture retries about once a second until the audio stack is ready and
  reconnects if you switch output devices.
- **Not listed in the screen-saver dropdown:** use right-click → **Install** on the
  `.scr`, or the `reg add` lines in Step 3.
- **Hotkey does nothing:** the shortcut must be on the Desktop or in the Start Menu,
  and the combo must not already be taken by another app.
- **After a rebuild:** re-copy the new build output over your install folder. The
  shortcut and the screen-saver setting point at *that* copy.
- **Runs on the integrated GPU:** that's fine for a screensaver. To force the
  dedicated GPU: Settings → System → Display → Graphics → add `RoomSaver.scr` →
  High performance.

## Notes

- Every screen showing the room is a separate full render; on a weak GPU with several
  large screens, choose fewer screens if it stutters.
- Audio capture is shared-mode WASAPI loopback. It never locks the audio device or
  interferes with other apps.
