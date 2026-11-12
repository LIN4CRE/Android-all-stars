// The hub: see hub.h.
#include "app/hub.h"
#include <algorithm>
#include <cmath>

namespace {
ColorF mix(ColorF a, ColorF b, float t) {
  return colorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
ColorF withA(ColorF c, float a) { return colorF(c.r, c.g, c.b, a); }
const ColorF kGold = {0.98f, 0.78f, 0.22f, 1.0f};
const ColorF kInk = {0.035f, 0.045f, 0.085f, 1.0f};
const ColorF kPanel = {0.105f, 0.125f, 0.205f, 1.0f};
const ColorF kText = {0.96f, 0.97f, 1.0f, 1.0f};
const ColorF kSub = {0.72f, 0.76f, 0.86f, 1.0f};

bool inside(const RectF& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
float cx(const RectF& r) { return (r.left + r.right) * 0.5f; }
float cy(const RectF& r) { return (r.top + r.bottom) * 0.5f; }
bool isGame(Hub::Action a) { return a >= Hub::kPlaySMB1 && a <= Hub::kPlayAllStars; }
}  // namespace

const char* Hub::title(Action a) {
  switch (a) {
    case kContinue: return "Continue Playing";
    case kImport: return "Import Your Game File";
    case kPlaySMB1: return "Super Mario Bros.";
    case kPlaySMBLL: return "The Lost Levels";
    case kPlaySMB2: return "Super Mario Bros. 2";
    case kPlaySMB3: return "Super Mario Bros. 3";
    case kPlayAllStars: return "All-Stars Title Screen";
    case kGameFile: return "Game File";
    case kSaves: return "Save Management";
    case kControls: return "Controls";
    case kDisplay: return "Display";
    case kAudio: return "Audio";
    case kAchievements: return "Achievements";
    case kMusic: return "Music Player";
    case kHelp: return "Help";
    case kAbout: return "About";
    case kUpdates: return "Updates";
    case kExit: return "Exit";
    default: return "";
  }
}

ColorF Hub::gameColor(Action a) {
  switch (a) {
    case kPlaySMB1: return colorF(0.86f, 0.27f, 0.22f);   // red
    case kPlaySMBLL: return colorF(0.22f, 0.62f, 0.32f);  // green
    case kPlaySMB2: return colorF(0.50f, 0.38f, 0.86f);   // purple
    case kPlaySMB3: return colorF(0.90f, 0.52f, 0.14f);   // orange
    default: return colorF(0.80f, 0.60f, 0.16f);          // gold
  }
}

std::string Hub::subtitle(Action a) const {
  switch (a) {
    case kContinue: return state_.resumeLine;
    case kImport:
      return "SMAS Mobile runs your own copy of Super Mario All-Stars (USA). Choose the file to begin.";
    case kGameFile: return state_.gameLine;
    case kSaves: return state_.savesLine;
    case kControls: return "Touch, controllers";
    case kDisplay: return "Picture, filters";
    case kAudio: return "Volume, music packs";
    case kAchievements: return state_.achievementsLine;
    case kMusic: return "The games' soundtracks";
    case kHelp: return "Guide, troubleshooting";
    case kAbout: return "Licences, credits";
    case kUpdates: return "Version " + state_.version;
    case kExit: return "Saves, then closes";
    default: return "";
  }
}

void Hub::setState(const State& s) {
  const bool changed = s.haveGame != state_.haveGame || s.haveResume != state_.haveResume;
  state_ = s;
  if (changed) {
    tiles_.clear();  // (the wide bar comes or goes)
    // The focus starts on what the player most likely wants.
    focus_ = !s.haveGame ? kImport : s.haveResume ? kContinue : focus_;
  }
  if (!s.touch) ring_ = true;
}

// Rows, top to bottom, in units of u = 1/8 of the usable height: the header;
// the wide bar (Continue Playing, or Import while there is no game file);
// the five game cards; two rows of tiles.
std::vector<Hub::Tile> Hub::layout(int W, int H, const float in[4]) const {
  std::vector<Tile> t;
  const float L = in[0], T = in[1], R = (float)W - in[2], B = (float)H - in[3];
  const float w = R - L, h = B - T;
  if (w < 64 || h < 64) return t;
  const float u = h / 8.0f, gap = u * 0.14f, mx = std::max(u * 0.3f, w * 0.02f);
  const float x0 = L + mx, x1 = R - mx;
  float y = T + u * 0.2f + u * 0.9f + gap;  // (under the header)
  const bool bar = !state_.haveGame || state_.haveResume;
  const float barH = bar ? u * 1.15f : 0.0f, tileH = u * 1.02f;
  const float cardsH = h - (y - T) - (bar ? barH + gap : 0.0f) - 2 * (tileH + gap) - u * 0.2f;
  if (bar) {
    Tile b;
    b.action = state_.haveGame ? kContinue : kImport;
    b.kind = 1;
    b.r = rectF(x0, y, x1, y + barH);
    t.push_back(b);
    y += barH + gap;
  }
  static const Action kGames[5] = {kPlaySMB1, kPlaySMBLL, kPlaySMB2, kPlaySMB3, kPlayAllStars};
  const float cw = (x1 - x0 - gap * 4) / 5.0f;
  for (int i = 0; i < 5; i++) {
    Tile c;
    c.action = kGames[i];
    c.kind = 2;
    c.enabled = state_.haveGame;
    c.r = rectF(x0 + i * (cw + gap), y, x0 + i * (cw + gap) + cw, y + cardsH);
    t.push_back(c);
  }
  y += cardsH + gap;
  static const Action kRow1[6] = {kGameFile, kSaves, kControls, kDisplay, kAudio, kAchievements};
  static const Action kRow2[5] = {kMusic, kHelp, kAbout, kUpdates, kExit};
  const float tw1 = (x1 - x0 - gap * 5) / 6.0f, tw2 = (x1 - x0 - gap * 4) / 5.0f;
  for (int i = 0; i < 6; i++) {
    Tile k;
    k.action = kRow1[i];
    k.r = rectF(x0 + i * (tw1 + gap), y, x0 + i * (tw1 + gap) + tw1, y + tileH);
    t.push_back(k);
  }
  y += tileH + gap;
  for (int i = 0; i < 5; i++) {
    Tile k;
    k.action = kRow2[i];
    k.enabled = k.action != kMusic || state_.haveGame;
    k.r = rectF(x0 + i * (tw2 + gap), y, x0 + i * (tw2 + gap) + tw2, y + tileH);
    t.push_back(k);
  }
  return t;
}

void Hub::ensureLayout() {
  if (tiles_.empty() && lastW_ > 0) tiles_ = layout(lastW_, lastH_, lastInsets_);
  if (!find(focus_) && !tiles_.empty()) focus_ = tiles_[0].action;
}

const Hub::Tile* Hub::find(Action a) const {
  for (const Tile& t : tiles_)
    if (t.action == a) return &t;
  return nullptr;
}

void Hub::focus(Action a) {
  focus_ = a;
  ensureLayout();
}

Hub::Action Hub::focused() const { return focus_; }

ColorF Hub::accent() const { return isGame(focus_) ? gameColor(focus_) : kGold; }

// A tile used: its action; one that needs the game file first asks for it.
Hub::Action Hub::useTile(const Tile& t) const { return t.enabled ? t.action : kImport; }

// The nearest tile in a direction: the one whose centre is least far, the
// sideways distance counting double (so a move keeps to its column).
void Hub::move(int dx, int dy) {
  ensureLayout();
  const Tile* cur = find(focus_);
  if (!cur) return;
  const Tile* best = nullptr;
  float bestScore = 1e18f;
  for (const Tile& t : tiles_) {
    if (&t == cur) continue;
    const float ax = cx(t.r) - cx(cur->r), ay = cy(t.r) - cy(cur->r);
    float along, across;
    if (dx) {
      // (sideways: only within the row)
      if (std::fabs(ay) > (cur->r.bottom - cur->r.top) * 0.5f) continue;
      along = ax * (float)dx;
      across = std::fabs(ay);
    } else {
      along = ay * (float)dy;
      across = std::fabs(ax);
      // (a tile right above or below, even partly, beats one off to the side)
      if (t.r.right > cur->r.left && t.r.left < cur->r.right) across *= 0.25f;
    }
    if (along <= 1.0f) continue;
    const float score = along + across * 2.0f;
    if (score < bestScore) bestScore = score, best = &t;
  }
  if (best) focus_ = best->action;
}

Hub::Action Hub::update(const MenuInput& in, float dt) {
  time_ += dt;
  const int dir = in.up ? 1 : in.down ? 2 : in.left ? 3 : in.right ? 4 : 0;
  bool fire = false;
  if (dir != repeatDir_) {
    repeatDir_ = dir;
    repeat_ = 0.38f;
    fire = dir != 0;
  } else if (dir != 0) {
    repeat_ -= dt;
    if (repeat_ <= 0) {
      repeat_ = 0.12f;
      fire = true;
    }
  }
  if (fire) {
    // (the first press only shows where the focus is)
    if (!ring_) ring_ = true;
    else move(dir == 3 ? -1 : dir == 4 ? 1 : 0, dir == 1 ? -1 : dir == 2 ? 1 : 0);
  }
  if (in.confirm) {
    ensureLayout();
    ring_ = true;
    if (const Tile* t = find(focus_)) return useTile(*t);
  }
  if (in.back) return kExit;
  return kNone;
}

Hub::Action Hub::tap(float x, float y) {
  ensureLayout();
  for (const Tile& t : tiles_) {
    if (!inside(t.r, x, y)) continue;
    focus_ = t.action;
    ring_ = false;
    return useTile(t);
  }
  return kNone;
}

void Hub::draw(Menu& menu, int W, int H, const float in[4]) {
  Canvas* cv = menu.canvas();
  if (!cv) return;
  if (W != lastW_ || H != lastH_ || tiles_.empty() || !std::equal(in, in + 4, lastInsets_)) {
    lastW_ = W;
    lastH_ = H;
    std::copy(in, in + 4, lastInsets_);
    tiles_ = layout(W, H, in);
  }
  ensureLayout();
  const float L = in[0], T = in[1], R = (float)W - in[2], B = (float)H - in[3];
  const float u = (B - T) / 8.0f, mx = std::max(u * 0.3f, (R - L) * 0.02f);
  const float line = std::max(2.0f, u * 0.035f);

  // The backdrop: night blue, lighter towards the bottom, a few soft discs.
  cv->fillGradient(rectF(0, 0, (float)W, (float)H), colorF(0.045f, 0.06f, 0.14f), colorF(0.10f, 0.13f, 0.27f));
  const float dr = (float)H * 0.55f;
  cv->fillRound(rectF((float)W * 0.78f - dr, -dr * 1.1f, (float)W * 0.78f + dr, dr * 0.9f), dr, colorF(1, 1, 1, 0.025f));
  cv->fillRound(rectF(-dr * 0.6f, (float)H - dr * 0.7f, dr * 1.4f, (float)H + dr * 1.3f), dr, colorF(1, 1, 1, 0.02f));

  // The header: the app's name; whose it is, and what it is not.
  const RectF head = rectF(L + mx, T + u * 0.2f, R - mx, T + u * 1.1f);
  menu.displayText(L"SMAS Mobile", rectF(head.left, head.top, head.right, head.bottom), u * 0.62f, kGold, kInk,
                   std::max(2.0f, u * 0.05f), kAlignLeading);
  menu.text(L"LIN4CRE", rectF(head.left, head.top, head.right, head.top + u * 0.42f), u * 0.3f, kWeightBold, kText,
            kAlignTrailing);
  menu.text(L"Unofficial. Not affiliated with or endorsed by Nintendo.",
            rectF(head.left, head.top + u * 0.44f, head.right, head.bottom), u * 0.2f, kWeightNormal, kSub, kAlignTrailing);

  const float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
  for (const Tile& t : tiles_) {
    const bool hot = ring_ && t.action == focus_;
    const float rad = u * (t.kind == 2 ? 0.2f : 0.16f);
    const RectF r = t.r;
    const float w = r.right - r.left, h = r.bottom - r.top, pad = std::min(w, h) * 0.12f;
    ColorF base = kPanel;
    if (t.kind == 2) base = gameColor(t.action);
    else if (t.kind == 1) base = t.action == kImport ? colorF(0.80f, 0.60f, 0.16f) : colorF(0.16f, 0.46f, 0.80f);
    else if (t.action == kExit) base = colorF(0.20f, 0.13f, 0.16f);
    if (!t.enabled) base = mix(base, kPanel, 0.72f);
    // a soft shadow, the tile, a lighter top edge
    cv->fillRound(rectF(r.left + line, r.top + line * 1.8f, r.right + line, r.bottom + line * 1.8f), rad,
                  colorF(0, 0, 0, 0.35f));
    cv->fillRound(r, rad, base);
    cv->fillRound(rectF(r.left, r.top, r.right, r.top + h * 0.5f), rad, colorF(1, 1, 1, t.kind ? 0.10f : 0.045f));
    cv->strokeRound(r, rad, hot ? line * 2.0f : line * 0.6f,
                    hot ? withA(mix(kGold, colorF(1, 1, 1), pulse * 0.5f), 1.0f) : colorF(1, 1, 1, 0.16f));
    const ColorF ink = t.enabled ? kText : withA(kText, 0.55f);
    const std::wstring name = Menu::widen(title(t.action));
    const std::wstring sub = Menu::widen(subtitle(t.action));
    auto fit = [&](const std::wstring& s, float size, int weight, float room) {
      return size * std::min(1.0f, room / std::max(1.0f, cv->measure(s, size, weight)));
    };
    if (t.kind == 2) {
      // A game card: an emblem of plain shapes (its number in a ring; a star
      // for the cartridge's own title screen), its name below.
      const float er = std::min(w, h * 0.62f) * 0.34f, ex = cx(r), ey = r.top + h * 0.40f;
      cv->fillRound(rectF(ex - er, ey - er, ex + er, ey + er), er, colorF(0, 0, 0, 0.22f));
      cv->strokeEllipse(ex, ey, er, er, line * 1.4f, withA(ink, t.enabled ? 0.9f : 0.5f));
      if (t.action == kPlayAllStars) {
        menu.star(ex, ey, er * 0.62f, withA(kGold, t.enabled ? 1.0f : 0.5f), kInk, line);
      } else {
        const wchar_t* mark = t.action == kPlaySMB1 ? L"1" : t.action == kPlaySMBLL ? L"LL" : t.action == kPlaySMB2 ? L"2" : L"3";
        cv->titleText(mark, rectF(ex - er, ey - er, ex + er, ey + er + er * 0.12f), er * (t.action == kPlaySMBLL ? 0.95f : 1.25f),
                      ink, kAlignCenter, kParaCenter);
      }
      const float ts = fit(name, u * 0.27f, kWeightBold, w - pad * 2);
      menu.text(name, rectF(r.left + pad, r.bottom - h * 0.30f, r.right - pad, r.bottom - h * 0.12f), ts, kWeightBold, ink,
                kAlignCenter);
      if (!t.enabled) {
        const std::wstring need = L"Needs your game file";
        menu.text(need, rectF(r.left + pad, r.bottom - h * 0.13f, r.right - pad, r.bottom - h * 0.02f),
                  fit(need, u * 0.19f, kWeightNormal, w - pad * 2), kWeightNormal, withA(kText, 0.6f), kAlignCenter);
      }
    } else if (t.kind == 1) {
      // The wide bar: a play mark (or a plus), the title and what it stands for.
      const float mr = h * 0.30f, mxp = r.left + pad + mr, myp = cy(r);
      cv->fillRound(rectF(mxp - mr, myp - mr, mxp + mr, myp + mr), mr, colorF(0, 0, 0, 0.25f));
      if (t.action == kContinue) {
        const PointF tri[3] = {{mxp - mr * 0.32f, myp - mr * 0.5f}, {mxp + mr * 0.55f, myp}, {mxp - mr * 0.32f, myp + mr * 0.5f}};
        cv->fillPolygon(tri, 3, kText);
      } else {
        cv->fillRect(rectF(mxp - mr * 0.5f, myp - mr * 0.1f, mxp + mr * 0.5f, myp + mr * 0.1f), kText);
        cv->fillRect(rectF(mxp - mr * 0.1f, myp - mr * 0.5f, mxp + mr * 0.1f, myp + mr * 0.5f), kText);
      }
      const float tx = mxp + mr + pad, room = r.right - pad - tx;
      menu.text(name, rectF(tx, r.top + h * 0.12f, r.right - pad, r.top + h * 0.56f), fit(name, u * 0.36f, kWeightBold, room),
                kWeightBold, kText);
      menu.text(sub, rectF(tx, r.top + h * 0.54f, r.right - pad, r.bottom - h * 0.12f), fit(sub, u * 0.23f, kWeightNormal, room),
                kWeightNormal, withA(kText, 0.85f));
    } else {
      const float room = w - pad * 2;
      menu.text(name, rectF(r.left + pad, r.top + h * 0.14f, r.right - pad, r.top + h * 0.56f),
                fit(name, u * 0.27f, kWeightBold, room), kWeightBold, ink);
      menu.text(sub, rectF(r.left + pad, r.top + h * 0.54f, r.right - pad, r.bottom - h * 0.12f),
                fit(sub, u * 0.205f, kWeightNormal, room), kWeightNormal, withA(kSub, t.enabled ? 1.0f : 0.55f));
    }
  }
}
