// The few things only the operating system can do for the app. Implemented
// for Android in platform/android/app_android.cpp (through SmasActivity);
// the host tests supply their own (tests/host/fake_platform.cpp).
//
// Files chosen with the system's pickers never reach the app as paths into
// other apps' storage: the Android side copies the chosen document into the
// app's own temporary folder (and a saved document out of it), so the native
// code only ever opens files it owns.
#pragma once
#include <string>
#include "app/canvas.h"

namespace platform {

// A web page in the system's browser (the app itself has no network access).
void openUrl(const char* url);

// A short tick of vibration, where the device has it (no permission needed).
void haptic();

// The part of the screen the system keeps at each edge (a display cutout,
// rounded corners), in pixels. False if it can't be told (taken as none).
bool safeInsets(float* left, float* top, float* right, float* bottom);

// Where the on-screen controls are, so that the system's edge gestures keep
// off them while a game is played (none: n = 0).
void setGestureExclusion(const IRect* rects, int n);

enum class PickKind { GameFile, Backup };
// Opens the system's file picker. What is chosen is copied to `to` (no more
// than `maxBytes` of it: anything longer is refused). Returns at once; the
// outcome is polled. False if the picker could not be opened.
bool pickDocument(PickKind kind, const std::wstring& to, long long maxBytes);
// Opens the system's "save as" picker; `from` is copied to the place chosen.
bool saveDocument(const std::wstring& from, const std::string& suggestedName, const char* mimeType);

enum class PickResult {
  Pending,    // the picker is still open
  Cancelled,  // closed without choosing
  Done,       // copied
  TooLarge,   // the document chosen is longer than maxBytes
  Failed,     // it could not be read or written (see `error`)
};
// `shownName`: the chosen document's own name, for messages.
PickResult pollDocument(std::string* shownName, std::string* error);

// The display's density: how many pixels one density-independent pixel (dp)
// is (2.625 on a typical 1080-line phone), and one scale-independent pixel
// (sp: the same times the system's font-size setting). The menus' text and
// rows are sized from these, so they are a readable, touchable size on a
// phone and on a tablet alike. False when the system does not say.
bool displayDensity(float* pxPerDp, float* pxPerSp);

// The games run at 60 frames a second. On a display that can run faster
// (90, 120, 144 Hz) the system is asked for its mode nearest 60 Hz while the
// app is on screen, so that every refresh is one whole frame (uneven motion
// at 120 Hz: upstream issue #23); or, with false, for its usual mode.
void preferSixtyHz(bool on);

// "Pixel 8, Android 15 (API 35)": for the About page and the log.
std::string deviceSummary();

}  // namespace platform
