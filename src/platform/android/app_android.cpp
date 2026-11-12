// SMAS Mobile: the Android side. The SDL window and event loop, frame pacing,
// Android's app lifecycle, and the few things only the system can do
// (app/platform.h), which go through SmasActivity by JNI. The application
// itself is app/app.cpp and is platform-independent.
#include <SDL.h>
#include <SDL_syswm.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include "app/app.h"
#include "app/platform.h"
#include "app/sys.h"
#include "app/vkeys.h"

namespace {
// A call into SmasActivity's static methods. Local references are released
// and any Java exception is cleared when it goes out of scope: a failure on
// the Java side never takes the game down, the call just does nothing.
class ActivityCall {
public:
  ActivityCall() {
    env = (JNIEnv*)SDL_AndroidGetJNIEnv();  // (attaches this thread if need be)
    act = env ? (jobject)SDL_AndroidGetActivity() : nullptr;
    cls = act ? env->GetObjectClass(act) : nullptr;
  }
  ~ActivityCall() {
    if (!env) return;
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
    if (cls) env->DeleteLocalRef(cls);
    if (act) env->DeleteLocalRef(act);
  }
  ActivityCall(const ActivityCall&) = delete;
  ActivityCall& operator=(const ActivityCall&) = delete;
  jmethodID method(const char* name, const char* sig) {
    if (!cls) return nullptr;
    jmethodID m = env->GetStaticMethodID(cls, name, sig);
    if (!m && env->ExceptionCheck()) env->ExceptionClear();
    if (!m) __android_log_print(ANDROID_LOG_ERROR, "SmasMobile", "SmasActivity.%s%s not found", name, sig);
    return m;
  }
  bool failed() {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
  }
  std::string text(jstring s) {
    std::string out;
    if (!s) return out;
    if (const char* c = env->GetStringUTFChars(s, nullptr)) {
      out = c;
      env->ReleaseStringUTFChars(s, c);
    }
    env->DeleteLocalRef(s);
    return out;
  }
  JNIEnv* env = nullptr;
  jobject act = nullptr;
  jclass cls = nullptr;
};

std::string callString(const char* name) {
  ActivityCall j;
  jmethodID m = j.method(name, "()Ljava/lang/String;");
  if (!m) return "";
  jstring s = (jstring)j.env->CallStaticObjectMethod(j.cls, m);
  if (j.failed()) return "";
  return j.text(s);
}

// Tells Android the game runs at 60 fps (ANativeWindow_setFrameRate, Android
// 11+), so a phone with an adaptive display can run it at 60 Hz or a multiple
// of it; or, when the player chose the device's own rate, that the app has no
// preference. (SmasActivity.preferSixtyHz asks for the display mode as well,
// which also works before Android 11.)
bool g_sixtyHz = true;
void setFrameRate(SDL_Window* w) {
  typedef int32_t (*SetFrameRate)(ANativeWindow*, float, int8_t);
  static SetFrameRate fn = (SetFrameRate)dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (fn && w && SDL_GetWindowWMInfo(w, &info) && info.subsystem == SDL_SYSWM_ANDROID && info.info.android.window)
    fn(info.info.android.window, g_sixtyHz ? (float)Snes::kFrameRate : 0.0f,
       g_sixtyHz ? 1 /* ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE */ : 0 /* ..._DEFAULT */);
}

// Refreshes per game frame when the display rate is a whole multiple of the
// game's 60.1 fps, else 0. (Phones' rates are 60, 90, 120 Hz and the like;
// the measured one wanders a little.)
int lockedRefreshes(double hz) {
  int k = (int)std::lround(hz / Snes::kFrameRate);
  return k >= 1 && std::fabs(hz / k - Snes::kFrameRate) / Snes::kFrameRate < 0.025 ? k : 0;
}

// The log: a short plain-text record of what the app did (no personal data),
// in the app's own folder on shared storage where it can be fetched over USB.
// The previous run's is kept beside it, for a problem that ended the app.
void openLog(const std::wstring& dir) {
  const std::wstring path = pathJoin(dir, kLogFileName);
  if (fileSize(path) > 0) renameFile(path, path + L".1");
  g_log = openFile(path, "w");
}
}  // namespace

// ---------------------------------------------------------------------------
// app/platform.h

namespace platform {

void openUrl(const char* url) {
  if (SDL_OpenURL(url) != 0) logf("could not open a browser: %s", SDL_GetError());
}

void haptic() {
  ActivityCall j;
  if (jmethodID m = j.method("haptic", "()V")) j.env->CallStaticVoidMethod(j.cls, m);
}

bool safeInsets(float* left, float* top, float* right, float* bottom) {
  ActivityCall j;
  jmethodID m = j.method("safeInsets", "()[I");
  if (!m) return false;
  jintArray a = (jintArray)j.env->CallStaticObjectMethod(j.cls, m);
  if (j.failed() || !a) return false;
  jint v[4] = {0, 0, 0, 0};
  const bool ok = j.env->GetArrayLength(a) >= 4;
  if (ok) j.env->GetIntArrayRegion(a, 0, 4, v);
  j.env->DeleteLocalRef(a);
  if (!ok) return false;
  *left = (float)v[0];
  *top = (float)v[1];
  *right = (float)v[2];
  *bottom = (float)v[3];
  return true;
}

void setGestureExclusion(const IRect* rects, int n) {
  ActivityCall j;
  jmethodID m = j.method("setGestureExclusion", "([I)V");
  if (!m) return;
  n = std::clamp(n, 0, 32);
  jintArray a = j.env->NewIntArray(n * 4);
  if (!a) return;
  for (int i = 0; i < n; i++) {
    const jint v[4] = {rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    j.env->SetIntArrayRegion(a, i * 4, 4, v);
  }
  j.env->CallStaticVoidMethod(j.cls, m, a);
  j.env->DeleteLocalRef(a);
}

bool pickDocument(PickKind kind, const std::wstring& to, long long maxBytes) {
  ActivityCall j;
  jmethodID m = j.method("pickDocument", "(ILjava/lang/String;J)Z");
  if (!m) return false;
  jstring path = j.env->NewStringUTF(narrowUtf8(to).c_str());
  if (!path) return false;
  const jboolean ok = j.env->CallStaticBooleanMethod(j.cls, m, (jint)(kind == PickKind::GameFile ? 0 : 1), path,
                                                     (jlong)maxBytes);
  j.env->DeleteLocalRef(path);
  return !j.failed() && ok;
}

bool saveDocument(const std::wstring& from, const std::string& suggestedName, const char* mimeType) {
  ActivityCall j;
  jmethodID m = j.method("saveDocument", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
  if (!m) return false;
  jstring path = j.env->NewStringUTF(narrowUtf8(from).c_str());
  jstring name = j.env->NewStringUTF(suggestedName.c_str());
  jstring mime = j.env->NewStringUTF(mimeType);
  jboolean ok = JNI_FALSE;
  if (path && name && mime) ok = j.env->CallStaticBooleanMethod(j.cls, m, path, name, mime);
  if (path) j.env->DeleteLocalRef(path);
  if (name) j.env->DeleteLocalRef(name);
  if (mime) j.env->DeleteLocalRef(mime);
  return !j.failed() && ok;
}

PickResult pollDocument(std::string* shownName, std::string* error) {
  jint state = 0;
  {
    ActivityCall j;
    jmethodID m = j.method("pollDocument", "()I");
    if (!m) return PickResult::Failed;
    state = j.env->CallStaticIntMethod(j.cls, m);
    if (j.failed()) return PickResult::Failed;
  }
  // (SmasActivity: 0 open, 1 cancelled, 2 done, 3 too large, 4 failed)
  if (state == 0) return PickResult::Pending;
  if (shownName) *shownName = callString("pickedName");
  if (error) *error = callString("pickError");
  switch (state) {
    case 1: return PickResult::Cancelled;
    case 2: return PickResult::Done;
    case 3: return PickResult::TooLarge;
    default: return PickResult::Failed;
  }
}

void preferSixtyHz(bool on) {
  g_sixtyHz = on;
  if (g_app) setFrameRate(g_app->window);
  ActivityCall j;
  if (jmethodID m = j.method("preferSixtyHz", "(Z)V")) j.env->CallStaticVoidMethod(j.cls, m, (jboolean)on);
}

bool displayDensity(float* pxPerDp, float* pxPerSp) {
  ActivityCall j;
  jmethodID m = j.method("displayDensity", "()[F");
  if (!m) return false;
  jfloatArray a = (jfloatArray)j.env->CallStaticObjectMethod(j.cls, m);
  if (j.failed() || !a) return false;
  jfloat v[2] = {0, 0};
  const bool ok = j.env->GetArrayLength(a) >= 2;
  if (ok) j.env->GetFloatArrayRegion(a, 0, 2, v);
  j.env->DeleteLocalRef(a);
  if (!ok || !(v[0] > 0) || !(v[1] > 0)) return false;
  *pxPerDp = v[0];
  *pxPerSp = v[1];
  return true;
}

std::string deviceSummary() {
  const std::string s = callString("deviceSummary");
  return s.empty() ? "Android device" : s;
}

}  // namespace platform

// "Basic mode" controllers (see input_sdl.h): SmasActivity passes on the
// buttons of a pad that SDL does not treat as a controller.
extern "C" JNIEXPORT void JNICALL Java_site_linacre_smas_SmasActivity_nativeBasicPad(JNIEnv*, jclass, jint buttons,
                                                                                    jfloat x, jfloat y) {
  inputBasicPad((uint32_t)buttons, x, y);
}

extern "C" JNIEXPORT void JNICALL Java_site_linacre_smas_SmasActivity_nativeBasicPadName(JNIEnv* env, jclass,
                                                                                        jstring name) {
  if (!name) return;
  if (const char* c = env->GetStringUTFChars(name, nullptr)) {
    inputBasicPadName(c);
    env->ReleaseStringUTFChars(name, c);
  }
}

// ---------------------------------------------------------------------------

bool App::init() {
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");  // Back is a key (the menu, back), not "close the app"
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, "SmasMobile", "SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  window = SDL_CreateWindow("SMAS Mobile", 0, 0, 0, 0,
                            SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    __android_log_print(ANDROID_LOG_ERROR, "SmasMobile", "no window: %s", SDL_GetError());
    return false;
  }
  setFrameRate(window);

  // Everything of the player's (game file, saves, settings) is in the app's
  // private storage. Screenshots, music packs and the log are in its own
  // folder on shared storage (no permission needed), reachable over USB.
  const char* internal = SDL_AndroidGetInternalStoragePath();
  const char* external = SDL_AndroidGetExternalStoragePath();
  const std::wstring dataDir = widenUtf8(internal ? internal : ".");
  const std::wstring sharedDir = external && *external ? widenUtf8(external) : dataDir;
  openLog(sharedDir);

  if (!video.init(window)) {
    logf("video init failed: %s", SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMAS Mobile",
                             "The graphics could not be started. SMAS Mobile needs OpenGL ES 3.0, which this device "
                             "does not seem to provide.",
                             window);
    return false;
  }
  logf("video ok %dx%d", video.width(), video.height());
  start(dataDir, sharedDir);
  return true;
}

void App::shutdown() {
  if (!exeDir.empty()) stop();
  menu.setCanvas(nullptr);
  audio.shutdown();
  input.shutdown();
  video.shutdown();
  if (window) SDL_DestroyWindow(window);
  window = nullptr;
  if (g_log) fclose(g_log);
  g_log = nullptr;
  SDL_Quit();
}

// One pass per display refresh (SDL_GL_SwapWindow waits for it). The refresh
// rate is the median of recent intervals; on a whole multiple of the game's
// 60.1 fps each game frame gets a steady number of refreshes, else the game
// time owed decides.
void App::run() {
  const Uint64 freq = SDL_GetPerformanceFrequency();
  Uint64 last = SDL_GetPerformanceCounter();
  const double period = 1.0 / Snes::kFrameRate;
  float intervals[48] = {};
  int nIntervals = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      const float W = (float)video.width(), H = (float)video.height();
      switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_APP_TERMINATING:
          onTerminating();
          running = false;
          break;
        case SDL_APP_LOWMEMORY: onLowMemory(); break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) {
            int w = 0, h = 0;
            SDL_GL_GetDrawableSize(window, &w, &h);
            onResized(w, h);
          } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            focused = true;
          } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            focused = false;
            // (a system dialog or the notification shade over the game: nothing stays held)
            touch.releaseAll();
            input.clearKeys();
          }
          break;
        case SDL_DISPLAYEVENT:
          // (turned the other way round: the cutout is at the other side)
          refreshInsets();
          break;
        case SDL_RENDER_DEVICE_RESET:
          // (the graphics context was lost in the background: everything in it is made again)
          if (video.recover()) {
            onDeviceReset();
          } else {
            logf("the graphics context could not be rebuilt: %s", SDL_GetError());
            onTerminating();
            running = false;
          }
          break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          if (e.key.repeat) break;
          int vk = vkFromScancode(e.key.keysym.scancode);
          if (vk) input.onKey(vk, e.type == SDL_KEYDOWN);
          if (e.key.keysym.scancode != SDL_SCANCODE_AC_BACK && e.type == SDL_KEYDOWN) touchActive = false;
          break;
        }
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED: input.rescanDevices(); break;
        case SDL_FINGERDOWN:
          touchActive = true;
          touch.onDown((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H);
          break;
        case SDL_FINGERMOTION: touch.onMove((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H); break;
        case SDL_FINGERUP: touch.onUp((int64_t)e.tfinger.fingerId, e.tfinger.x * W, e.tfinger.y * H); break;
        case SDL_APP_WILLENTERBACKGROUND:
          // (Android may end the app at any time while it is in the background: everything is saved now)
          onBackground();
          break;
        case SDL_APP_DIDENTERFOREGROUND:
          last = SDL_GetPerformanceCounter();
          nIntervals = 0;
          pacing.vblanks = 0;
          pacing.accum = 0;
          setFrameRate(window);  // (the surface is new after a pause)
          onForeground();
          break;
        default: break;
      }
    }
    if (!running) break;
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (double)(now - last) / freq;
    last = now;
    intervals[nIntervals++ % 48] = (float)dt;
    double hz = 60.0;
    if (nIntervals >= 48) {
      float d[48];
      std::copy(intervals, intervals + 48, d);
      std::nth_element(d, d + 24, d + 48);
      if (d[24] > 0.002f) hz = 1.0 / d[24];
    } else {
      SDL_DisplayMode m;
      if (SDL_GetCurrentDisplayMode(0, &m) == 0 && m.refresh_rate > 0) hz = m.refresh_rate;
    }
    if (pacing.mode != kPaceVsync || lockedRefreshes(hz) != pacing.perFrame) {
      pacing = Pacing();
      pacing.mode = kPaceVsync;
      pacing.perFrame = lockedRefreshes(hz);
      logf("pacing: %.2f Hz display, %d refreshes per frame (0 = uneven)", hz, pacing.perFrame);
    }
    pacing.hz = hz;
    int frames = 0;
    int vb = std::max(1, (int)std::lround(dt * hz));  // refreshes since the last pass
    if (vb > 6) {  // stalled: resume without fast-forwarding
      vb = 0;
      pacing.vblanks = 0;
      pacing.accum = 0;
      frames = 1;
    }
    if (pacing.perFrame) {
      pacing.vblanks += vb;
      while (pacing.vblanks >= pacing.perFrame) {
        pacing.vblanks -= pacing.perFrame;
        frames++;
      }
    } else {
      pacing.accum += vb / hz;
      while (pacing.accum >= period) {
        pacing.accum -= period;
        frames++;
      }
    }
    frames = std::min(frames, 3);
    frame(dt, frames);
    render();
    video.present();
  }
}

extern "C" int SDL_main(int, char*[]) {
  // (A new App each time: after Exit, Android keeps the process, and opening
  // the app again calls this again.)
  std::unique_ptr<App> app = std::make_unique<App>();
  g_app = app.get();
  int rc = 0;
  if (!app->init()) {
    rc = 1;
  } else {
    app->run();
  }
  app->shutdown();
  g_app = nullptr;
  return rc;
}
