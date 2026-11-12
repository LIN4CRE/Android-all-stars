// A stand-in for Android: see fake_platform.h.
#include "fake_platform.h"
#include <algorithm>
#include <cstring>
#include "app/platform.h"
#include "app/sys.h"
#include "core/fileio.h"

namespace fake {
namespace {
Platform g_sys;
// the picker in progress
bool g_pickOpen = false, g_saveOpen = false;
std::wstring g_pickTo, g_saveFrom;
long long g_pickMax = 0;
std::string g_pickName, g_pickError;
}  // namespace

Platform& sys() { return g_sys; }
void reset() {
  g_sys = Platform();
  g_pickOpen = g_saveOpen = false;
}

std::vector<uint8_t> syntheticGameFile(uint8_t fill) {
  std::vector<uint8_t> d(0x200000, fill);
  static const char kTitle[] = "SUPER MARIO ALL_STARS";
  std::memcpy(&d[0x7fc0], kTitle, sizeof(kTitle) - 1);
  // sei; clc; xce; jmp $8003  (native mode, then idle)
  const uint8_t code[] = {0x78, 0x18, 0xfb, 0x4c, 0x03, 0x80};
  std::memcpy(&d[0x0000], code, sizeof code);
  // every interrupt vector at an rti; reset at $8000
  d[0x0010] = 0x40;
  for (int v = 0x7fe4; v < 0x8000; v += 2) d[v] = 0x10, d[v + 1] = 0x80;
  d[0x7ffc] = 0x00;
  d[0x7ffd] = 0x80;
  return d;
}

Session::Session(const std::wstring& data, const std::wstring& shared) : dataDir(data), sharedDir(shared) {
  app = std::make_unique<App>();
  g_app = app.get();
  app->video.init(nullptr);
  app->start(data, shared);
}

Session::~Session() {
  if (app) kill();
}

void Session::frames(int n, int gameFrames) {
  for (int i = 0; i < n; i++) {
    app->frame(1.0 / 60.0, gameFrames);
    g_sys.drawn.clear();
    app->render();
  }
}

void Session::tap(float x, float y) {
  app->touchActive = true;
  app->touch.onDown(7, x, y);
  app->touch.onUp(7, x, y);
  frames(2);
}

bool Session::choose(const std::string& label) {
  MenuPage* p = app->menu.top();
  if (!p) return false;
  for (MenuItem& it : p->items) {
    if (it.label != label) continue;
    if (it.enabled && !it.enabled()) return false;
    if (!it.onActivate) return false;
    auto fn = it.onActivate;  // (using it may rebuild the page)
    fn();
    return true;
  }
  return false;
}

bool Session::hasRow(const std::string& label) {
  MenuPage* p = app->menu.top();
  if (!p) return false;
  for (MenuItem& it : p->items)
    if (it.label == label) return true;
  return false;
}

std::string Session::pageTitle() { return app->menu.top() ? app->menu.top()->title : std::string(); }

std::string Session::pageText() {
  std::string out;
  MenuPage* p = app->menu.top();
  if (!p) return out;
  out = p->title + "\n" + p->subtitle + "\n";
  for (MenuItem& it : p->items) {
    out += it.label;
    if (it.valueText) out += " = " + it.valueText();
    out += "\n";
  }
  return out;
}

bool Session::drew(const std::wstring& text) {
  for (const Drawn& d : g_sys.drawn)
    if (d.kind == Drawn::Text && d.text.find(text) != std::wstring::npos) return true;
  return false;
}

void Session::kill() {
  // (no stop(): what is on disk is what there is)
  app->menu.setCanvas(nullptr);
  app.reset();
  g_app = nullptr;
}

void Session::exit() {
  app->stop();
  kill();
}

}  // namespace fake

// ---------------------------------------------------------------------------
// The video layer: a canvas that records, nothing else.

class GlesCanvas : public Canvas {
public:
  explicit GlesCanvas(unsigned epoch) : epoch_(epoch) {}
  unsigned epoch() const override { return epoch_; }
  void fillRect(RectF r, ColorF c) override {
    add(fake::Drawn::Fill, r);
    if (Canvas* m = mirror()) m->fillRect(r, c);
  }
  void strokeRect(RectF r, float w, ColorF c) override {
    add(fake::Drawn::Stroke, r);
    if (Canvas* m = mirror()) m->strokeRect(r, w, c);
  }
  void fillRound(RectF r, float rad, ColorF c) override {
    add(fake::Drawn::Fill, r);
    if (Canvas* m = mirror()) m->fillRound(r, rad, c);
  }
  void strokeRound(RectF r, float rad, float w, ColorF c) override {
    add(fake::Drawn::Stroke, r);
    if (Canvas* m = mirror()) m->strokeRound(r, rad, w, c);
  }
  void strokeEllipse(float cx, float cy, float rx, float ry, float w, ColorF c) override {
    add(fake::Drawn::Stroke, rectF(cx - rx, cy - ry, cx + rx, cy + ry));
    if (Canvas* m = mirror()) m->strokeEllipse(cx, cy, rx, ry, w, c);
  }
  void fillGradient(RectF r, ColorF top, ColorF bottom) override {
    add(fake::Drawn::Fill, r);
    if (Canvas* m = mirror()) m->fillGradient(r, top, bottom);
  }
  void fillPolygon(const PointF* pts, int n, ColorF c) override {
    if (Canvas* m = mirror()) m->fillPolygon(pts, n, c);
  }
  void strokePolygon(const PointF* pts, int n, float w, ColorF c) override {
    if (Canvas* m = mirror()) m->strokePolygon(pts, n, w, c);
  }
  int makeShape(const std::vector<PointF>& pts, const std::vector<int>& counts) override {
    if (Canvas* m = mirror()) return m->makeShape(pts, counts);
    return ++shapes_;
  }
  void fillShape(int shape, ColorF c, float scale, float tx, float ty) override {
    if (Canvas* m = mirror()) m->fillShape(shape, c, scale, tx, ty);
  }
  void freeShape(int) override {}
  void text(const std::wstring& s, RectF r, float size, int weight, ColorF c, TextAlign a, ParaAlign p) override {
    fake::Drawn d;
    d.kind = fake::Drawn::Text;
    d.r = r;
    d.text = s;
    d.size = size;
    d.width = measure(s, size, weight);
    fake::sys().drawn.push_back(d);
    if (Canvas* m = mirror()) m->text(s, r, size, weight, c, a, p);
  }
  // (a plain proportional font's average: a little over half the size per character)
  float measure(const std::wstring& s, float size, int weight) override {
    if (Canvas* m = mirror()) return m->measure(s, size, weight);
    return (float)s.size() * size * (weight >= kWeightBold ? 0.58f : 0.52f);
  }
  void titleText(const std::wstring& s, RectF r, float size, ColorF c, TextAlign a, ParaAlign p) override {
    fake::Drawn d;
    d.kind = fake::Drawn::Text;
    d.r = r;
    d.text = s;
    d.size = size;
    d.width = measureTitle(s, size);
    fake::sys().drawn.push_back(d);
    if (Canvas* m = mirror()) m->titleText(s, r, size, c, a, p);
  }
  float measureTitle(const std::wstring& s, float size) override {
    if (Canvas* m = mirror()) return m->measureTitle(s, size);
    return (float)s.size() * size * 0.6f;
  }
  void pushClip(RectF r) override {
    clips_++;
    if (Canvas* m = mirror()) m->pushClip(r);
  }
  void popClip() override {
    clips_--;
    if (Canvas* m = mirror()) m->popClip();
  }
  int makeImage(int w, int h, bool alpha) override {
    if (Canvas* m = mirror()) return m->makeImage(w, h, alpha);
    return ++images_;
  }
  void updateImage(int image, const uint32_t* px, int stride) override {
    if (Canvas* m = mirror()) m->updateImage(image, px, stride);
  }
  void drawImage(int image, RectF dst, float opacity, bool smooth) override {
    add(fake::Drawn::Image, dst);
    if (Canvas* m = mirror()) m->drawImage(image, dst, opacity, smooth);
  }
  void freeImage(int) override {}
  int clips_ = 0;

private:
  unsigned epoch_;
  int shapes_ = 0, images_ = 0;
  static Canvas* mirror() { return fake::sys().mirror; }
  void add(fake::Drawn::Kind k, RectF r) {
    fake::Drawn d;
    d.kind = k;
    d.r = r;
    fake::sys().drawn.push_back(d);
  }
};

Video::Video() = default;
Video::~Video() { shutdown(); }
bool Video::init(SDL_Window*) {
  w_ = fake::sys().width;
  h_ = fake::sys().height;
  static unsigned epochs = 0;
  canvas_ = std::make_unique<GlesCanvas>(++epochs);
  return true;
}
bool Video::recover() { return init(nullptr); }
bool Video::build() { return true; }
void Video::shutdown() { canvas_.reset(); }
void Video::resize(int w, int h) {
  if (w > 0 && h > 0) w_ = w, h_ = h;
}
Canvas* Video::canvas() { return canvas_.get(); }
void Video::upload(const uint32_t*, int, int, int) {}
void Video::draw(const VideoParams& p) {
  fake::Drawn d;
  d.kind = fake::Drawn::Image;
  d.r = rectF((float)p.dest.left, (float)p.dest.top, (float)p.dest.right, (float)p.dest.bottom);
  d.text = L"<game picture>";
  fake::sys().drawn.push_back(d);
  // (the preview tool: where the game's picture is; no game is drawn)
  if (Canvas* m = fake::sys().mirror) {
    m->fillRect(d.r, colorF(0.13f, 0.17f, 0.30f));
    m->strokeRect(d.r, 2.0f, colorF(1, 1, 1, 0.25f));
    m->text(L"The game's picture", d.r, (d.r.bottom - d.r.top) / 16.0f, kWeightNormal, colorF(1, 1, 1, 0.4f), kAlignCenter,
            kParaCenter);
  }
}
void Video::clear() {}
void Video::beginUi() {}
void Video::endUi() {}
void Video::present() {}
bool Video::capture(std::vector<uint32_t>&, int*, int*) { return false; }
void Video::pass(const VideoParams&, int, int, int, int, int) {}

// ---------------------------------------------------------------------------
// Sound: counted, not played.

bool Audio::init(int latencyMs) {
  latencyMs_ = latencyMs;
  dev_ = 1;
  return true;
}
void Audio::shutdown() { dev_ = 0; }
void Audio::push(const int16_t*, size_t frames, double) { fake::sys().audioFrames += (long long)frames; }
void Audio::flush() {}
void Audio::setPaused(bool) {}
int Audio::queuedFrames() const { return 0; }

// ---------------------------------------------------------------------------
// Controllers and keys: what the test says.

void inputBasicPad(uint32_t, float, float) {}
void inputBasicPadName(const char*) {}

bool Input::init() { return true; }
void Input::shutdown() { pads_.clear(); }
void Input::onKey(int vk, bool down) {
  if (vk <= 0 || vk >= 256) return;
  keys_[vk] = down;
  if (down) sticky_[vk] = 2;
}
void Input::clearKeys() {
  std::memset(keys_, 0, sizeof(keys_));
  std::memset(sticky_, 0, sizeof(sticky_));
}
bool Input::anyPad() const { return !pads_.empty() && pads_[0].connected; }
bool Input::anyPadActivity() const {
  if (pads_.empty() || !pads_[0].connected) return false;
  for (bool b : pads_[0].buttons)
    if (b) return true;
  return false;
}
int Input::newSlot() { return -1; }
void Input::scan() {}
void Input::poll() {
  for (uint8_t& s : sticky_)
    if (s) s--;
  const fake::Platform& f = fake::sys();
  if (!f.pad) {
    pads_.clear();
    return;
  }
  if (pads_.empty()) {
    pads_.push_back(PadState());
    pads_[0].name = L"Test Pad";
  }
  pads_[0].connected = true;
  for (int b = 0; b < 32; b++) pads_[0].buttons[b] = (f.padButtons >> b) & 1;
}
bool Input::active(const Binding& b, int padIdx, float dz) const {
  const PadState* p = pad(padIdx);
  switch (b.type) {
    case Binding::Key: return keyDown(b.code);
    case Binding::PadButton: return p && b.code < kPadButtons && p->buttons[b.code];
    case Binding::PadAxisPos: return p && b.code < 6 && p->axes[b.code] > dz;
    case Binding::PadAxisNeg: return p && b.code < 6 && p->axes[b.code] < -dz;
    case Binding::PadHat: return p && p->buttons[10 + (b.code & 3)];
    default: return false;
  }
}
void Input::resetCapture() {}
bool Input::capture(Binding*, bool, int, float) { return false; }

// ---------------------------------------------------------------------------
// app/platform.h

namespace platform {

void openUrl(const char* url) { fake::sys().urls.push_back(url); }
void haptic() { fake::sys().haptics++; }

bool safeInsets(float* l, float* t, float* r, float* b) {
  const fake::Platform& f = fake::sys();
  if (!f.insetsKnown) return false;
  *l = f.insets[0];
  *t = f.insets[1];
  *r = f.insets[2];
  *b = f.insets[3];
  return true;
}

void setGestureExclusion(const IRect* rects, int n) { fake::sys().exclusion.assign(rects, rects + n); }

bool pickDocument(PickKind, const std::wstring& to, long long maxBytes) {
  fake::Platform& f = fake::sys();
  if (!f.pickerOpens) return false;
  f.pickersOpened++;
  fake::g_pickOpen = true;
  fake::g_pickTo = to;
  fake::g_pickMax = maxBytes;
  return true;
}

bool saveDocument(const std::wstring& from, const std::string& suggestedName, const char* mimeType) {
  fake::Platform& f = fake::sys();
  if (!f.pickerOpens) return false;
  f.saversOpened++;
  fake::g_saveOpen = true;
  fake::g_saveFrom = from;
  f.savedName = suggestedName;
  f.savedMime = mimeType;
  return true;
}

// (as SmasActivity does it: the chosen document is copied into the app's own
// file, no more than maxBytes of it)
PickResult pollDocument(std::string* shownName, std::string* error) {
  fake::Platform& f = fake::sys();
  if (shownName) *shownName = f.documentName;
  if (error) error->clear();
  if (fake::g_saveOpen) {
    fake::g_saveOpen = false;
    if (f.answer == fake::Platform::Cancel) return PickResult::Cancelled;
    if (f.answer == fake::Platform::Fail) {
      if (error) *error = "the document provider refused";
      return PickResult::Failed;
    }
    f.saved.clear();
    if (!readFile(fake::g_saveFrom, f.saved, 256u << 20)) return PickResult::Failed;
    if (shownName) *shownName = f.savedName;
    return PickResult::Done;
  }
  if (!fake::g_pickOpen) return PickResult::Pending;
  fake::g_pickOpen = false;
  switch (f.answer) {
    case fake::Platform::Cancel: return PickResult::Cancelled;
    case fake::Platform::Fail:
      if (error) *error = "the document could not be opened";
      return PickResult::Failed;
    case fake::Platform::TooLarge: return PickResult::TooLarge;
    default: break;
  }
  if ((long long)f.document.size() > fake::g_pickMax) return PickResult::TooLarge;
  if (!writeFileSafely(fake::g_pickTo, f.document.data(), f.document.size())) return PickResult::Failed;
  return PickResult::Done;
}

void preferSixtyHz(bool on) { fake::sys().sixtyHz = on ? 1 : 0; }

bool displayDensity(float* pxPerDp, float* pxPerSp) {
  const fake::Platform& f = fake::sys();
  if (f.density <= 0) return false;
  *pxPerDp = f.density;
  *pxPerSp = f.density * f.fontScale;
  return true;
}

std::string deviceSummary() { return "Host test device"; }

}  // namespace platform
