# The app's source files, in one place: the Android build
# (android/app/src/main/cpp/CMakeLists.txt) and the host tests
# (tests/CMakeLists.txt) both include this, so neither can fall behind.
# SMAS_SRC is the path of this folder.

# The emulation core and the game modules (from the upstream port).
set(SMAS_CORE_SOURCES
  ${SMAS_SRC}/core/cpu65816.cpp
  ${SMAS_SRC}/core/apu.cpp
  ${SMAS_SRC}/core/dsp.cpp
  ${SMAS_SRC}/core/ppu.cpp
  ${SMAS_SRC}/core/snes.cpp
  ${SMAS_SRC}/core/rewind.cpp
  ${SMAS_SRC}/core/msu1.cpp
  ${SMAS_SRC}/core/png.cpp
  ${SMAS_SRC}/core/fileio.cpp
  ${SMAS_SRC}/core/zipfile.cpp
  ${SMAS_SRC}/core/disasm.cpp
  ${SMAS_SRC}/game/smas.cpp
  ${SMAS_SRC}/game/smb1.cpp
  ${SMAS_SRC}/game/smb1coop.cpp
  ${SMAS_SRC}/game/smb2.cpp
  ${SMAS_SRC}/game/smb3.cpp
  ${SMAS_SRC}/game/smb3coop.cpp
  ${SMAS_SRC}/game/achievements.cpp
  ${SMAS_SRC}/game/palette.cpp
)

# The application: platform-independent (built and tested on a host too).
set(SMAS_APP_SOURCES
  ${SMAS_SRC}/app/app.cpp
  ${SMAS_SRC}/app/app_hub.cpp
  ${SMAS_SRC}/app/app_saves.cpp
  ${SMAS_SRC}/app/hub.cpp
  ${SMAS_SRC}/app/config.cpp
  ${SMAS_SRC}/app/menu.cpp
  ${SMAS_SRC}/app/music.cpp
  ${SMAS_SRC}/app/achstore.cpp
  ${SMAS_SRC}/app/achexport.cpp
  ${SMAS_SRC}/app/msupack.cpp
  ${SMAS_SRC}/app/imageload.cpp
  ${SMAS_SRC}/app/romcheck.cpp
  ${SMAS_SRC}/app/savestore.cpp
  ${SMAS_SRC}/app/backup.cpp
  ${SMAS_SRC}/app/utf8.cpp
  ${SMAS_SRC}/app/sys_posix.cpp
  ${SMAS_SRC}/platform/android/touch.cpp
)

# The Android side: SDL window and events, OpenGL ES, audio, controllers, JNI.
set(SMAS_ANDROID_SOURCES
  ${SMAS_SRC}/platform/android/app_android.cpp
  ${SMAS_SRC}/platform/android/video_gles.cpp
  ${SMAS_SRC}/platform/android/audio_sdl.cpp
  ${SMAS_SRC}/platform/android/input_sdl.cpp
)
