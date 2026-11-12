# Upstream

SMAS Mobile is built on the open-source port **SuperMarioAllStars** by
**Darhkfox**.

| | |
| --- | --- |
| Project | <https://github.com/Darhkfox/SuperMarioAllStars> |
| Version taken | 1.1.1 (tag `v1.1.1`) |
| Commit | `0c49e5eec9ee6b33f72a30b904bf17bb19d21ccf` |
| Licence | MIT, Copyright (c) 2026 Darhkfox |

Upstream is a Windows application with an Android build. SMAS Mobile takes the
Android build and the code it shares with Windows, and is an Android app only.

## What was imported

One commit, "import: upstream SuperMarioAllStars v1.1.1, the Android subset"
(`27916cf`), holds 88 files copied from upstream without any change:

| Path | What it is |
| --- | --- |
| `src/core/` | the emulation core |
| `src/game/` | the game modules (widescreen, co-op, achievements and the rest) |
| `src/app/` | the parts of the application the Android build uses (menus, settings, music player, achievements store) |
| `src/platform/android/` | the Android side (SDL, OpenGL ES, audio, controllers, touch) |
| `android/` | the Gradle project |
| `third_party/fonts/` | the Luckiest Guy font and its licence |
| `third_party/stb/stb_image.h` | stb_image |
| `LICENSE` | the licence text |

To see everything SMAS Mobile changed:

```
git diff 27916cf HEAD -- src android third_party LICENSE
```

## What was not imported

* The Windows application (Direct2D canvas, XInput, the Windows launcher and
  its resources) and the desktop build files.
* The shader stack: librashader and the slang shader presets.
* The launcher and its embedded logo, and the online update check. SMAS Mobile
  has its own home screen and never goes online.
* Upstream's `gradle-wrapper.jar` and `gradlew` scripts. An official Gradle
  wrapper is to be added with the Android project's conversion.
* Documentation, release notes and packaging.

## What SMAS Mobile changes

New files:

| File | Purpose |
| --- | --- |
| `src/app/savestore.*` | the games' save file kept with a checksum, the two saves before it, restore points, damaged files set aside; save-state slots; the resume point |
| `src/app/romcheck.*` | checks a file the player chooses and says what it is before it is accepted |
| `src/app/backup.*`, `src/core/zipfile.*`, `src/core/crc32.h` | backups of saves and settings as one .zip, and restoring them |
| `src/app/hub.*`, `src/app/app_hub.cpp` | the home screen, the game file's import, Help and About |
| `src/app/app_saves.cpp` | saved progress in the application: save states, the resume point, Save Management |
| `src/app/platform.h` | the few things only Android can do (file pickers, vibration, the display's safe area and density) |
| `src/sources.cmake` | the one list of source files, for the Android build and the host tests |

Changed upstream files:

| File | Change |
| --- | --- |
| `src/app/app.h`, `src/app/app.cpp` | rewritten around the hub, the lifecycle (background, low memory, a lost graphics context) and the new save handling; the Windows paths removed |
| `src/app/config.*` | settings reworked for the app; values range-checked when read |
| `src/app/menu.*` | pages of wrapped text; rows and text sized for a phone from the display's density; panels kept inside the display's safe area; rows of different heights |
| `src/app/imageload.cpp` | a raw DEFLATE decoder exposed for the backup reader |
| `src/app/sys.h`, `src/app/sys_posix.cpp` | a file's size and time, removing and renaming, free storage added; the web request removed |
| `src/app/vkeys.h`, `src/app/audio.h`, `src/app/canvas.h` | the Windows branches removed; comments |
| `src/core/fileio.*` | files are written whole or not at all (temporary file, flushed to storage, then renamed) |
| `src/core/msu1.cpp` | the Windows branch removed |
| `src/platform/android/app_android.cpp` | rewritten: the system services through JNI, the lifecycle, the log |
| `src/platform/android/touch.*` | rewritten: a layout at the sides of the picture, the safe area, sizing, a layout editor, vibration |
| `src/platform/android/input_sdl.*` | controllers that SDL does not recognise as game controllers still work |
| `src/platform/android/video_gles.*` | everything in the graphics context can be made again after it is lost |
| `src/platform/android/audio_sdl.cpp` | non-Android branches removed |
| `LICENSE` | a second copyright line for the changes; the closing note describes this repository |

Not changed yet: `android/` (see the status in [README.md](../README.md)).

## Taking later upstream versions

There is no automatic link to upstream. To take a later version: import its
files in a commit of their own, as above, and then merge the changes by hand.
The files SMAS Mobile rewrote will not merge mechanically.
