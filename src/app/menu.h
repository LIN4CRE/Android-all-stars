// Overlay menu system drawn on a Canvas (canvas.h): stacked pages of
// items (actions, toggles, choices, sliders, key bindings, section headers),
// toast messages and the "achievement unlocked" banner.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "app/canvas.h"

struct MenuInput {
  bool up = false, down = false, left = false, right = false;
  bool confirm = false, back = false;
  bool tabLeft = false, tabRight = false;
  bool anyPressed() const { return up || down || left || right || confirm || back || tabLeft || tabRight; }
};

struct MenuItem {
  // Separator: a section header (its label, small, in the accent colour), or
  // a blank line when it has none. Label: a read-only row (value on the right).
  enum Kind { Action, Choice, Slider, Toggle, Submenu, Bind, Label, Separator };
  Kind kind = Action;
  std::string label;
  std::string help;
  std::function<void()> onActivate;               // Action / Submenu / Bind
  std::function<int()> get;                       // Choice/Slider/Toggle value
  std::function<void(int)> set;
  std::function<std::vector<std::string>()> options;  // Choice labels
  std::function<std::string()> valueText;         // custom value display (a note beside a toggle)
  std::function<bool()> enabled;
  int minV = 0, maxV = 100, step = 1;
  std::string suffix;
  bool wrap = true;

  static MenuItem action(std::string l, std::function<void()> f, std::string h = "");
  static MenuItem submenu(std::string l, std::function<void()> f, std::string h = "");
  static MenuItem toggle(std::string l, std::function<int()> g, std::function<void(int)> s, std::string h = "");
  static MenuItem choice(std::string l, std::vector<std::string> opts, std::function<int()> g,
                         std::function<void(int)> s, std::string h = "");
  static MenuItem slider(std::string l, int mn, int mx, int st, std::function<int()> g, std::function<void(int)> s,
                         std::string suffix = "", std::string h = "");
  static MenuItem info(std::string l, std::string h = "");
  static MenuItem separator(std::string l = "");
  static MenuItem header(std::string l) { return separator(std::move(l)); }
};

class Menu;
struct MenuPage {
  std::string title;
  std::string subtitle;
  std::vector<MenuItem> items;
  int sel = 0;
  float scroll = 0;  // pixels the list is scrolled by (eases towards the selection)
  bool freeScroll = false;  // (touch: dragged, so it doesn't follow the selection)
  bool wide = false;  // a wider panel (long lists, side panels)
  std::function<void(MenuPage&)> rebuild;  // called on enter / refresh
  // Optional custom content (drawn in place of the item list)
  std::function<void(Menu&, Canvas*, RectF)> drawCustom;
  std::function<bool(Menu&, const MenuInput&)> inputCustom;  // return true if consumed
  std::function<void()> onLeave;
  float listFraction = 1.0f;  // portion of the panel used by the list (rest: custom panel)
};

class Menu {
public:
  // The canvas to draw on (the video layer's; a new one after its device was rebuilt).
  void setCanvas(Canvas* c) { cv_ = c; }

  bool open() const { return !stack_.empty(); }
  void push(std::shared_ptr<MenuPage> p);
  void pop();
  void closeAll();
  MenuPage* top() { return stack_.empty() ? nullptr : stack_.back().get(); }
  size_t depth() const { return stack_.size(); }
  void refresh();
  // A Yes / No page (No selected) over the current one.
  void confirm(const std::string& question, std::function<void()> yes, const std::string& detail = "");
  // A paragraph as lines that fit a page's rows (a wide page's when `wide`):
  // for pages of explanation (Help, About, why a file was not accepted).
  std::vector<std::string> wrapText(const std::string& text, bool wide = false) const;
  // ... added to a page as read-only rows.
  void addText(MenuPage& page, const std::string& text) const;
  // For a page of text with a few actions: it opens at its top (not at its
  // first action), and up and down scroll it, moving onto an action once that
  // is in view. (Dragging scrolls any page.)
  static void makeScrollable(MenuPage& page);

  // Rebinding capture state (set by the app)
  bool capturing = false;

  // Touch screens: a close button on the panel, and taps and drags on its rows.
  bool touchUi = false;
  // The footer's line of keys: shown while keys or a controller are in use
  // (not while fingers are: the rows and the close button speak for themselves).
  bool showKeys = true;
  std::wstring keysHint;  // (another text for it; empty: the keyboard and pad's)
  bool confirmRight = false;  // (the pad's right face button confirms: the footer says so)
  // How the menus are sized for the screen in hand (set by the app from the
  // display's density). textScale: 1 is the size for a monitor or a TV, where
  // the screen's height is the measure of everything; a phone's small screen
  // needs larger text (about 1.5). minRow: the least height, in pixels, of a
  // row that can be chosen, so that a finger can hit it (0: the text's own).
  float textScale = 1.0f;
  float minRow = 0;
  // The part of the screen clear of a display cutout and rounded corners: the
  // insets at the left, top, right and bottom, in pixels. Panels, toasts and
  // the on-screen notes keep inside it.
  void setSafeArea(float left, float top, float right, float bottom);
  // The panel and the list as laid out for a screen (also used by the tests).
  struct Metrics {
    float s = 1;         // pixels per design unit: 1 on a 1080-line screen, times the text scale
    float titleScale = 1;  // ... for titles (they grow less)
    RectF safe{};        // the screen inside the insets
    RectF panel{};
    float pad = 0;       // the panel's inner margin
    float sizeBody = 0, sizeSmall = 0, sizeHelp = 0;
    float rowText = 0;   // the height of a line of text or a heading
    float rowTouch = 0;  // the height of a row that can be chosen
  };
  Metrics metrics(int w, int h, bool wide) const;
  // Where the last draw put the rows that can be tapped, and the close button (tests).
  struct RowHit {
    RectF r;
    int index;
  };
  const std::vector<RowHit>& rowHits() const { return rowHits_; }
  RectF closeHit() const { return closeHit_; }
  RectF listRect() const { return listRect_; }
  void tap(float x, float y);
  void drag(float dy);
  std::string captureText;

  void update(const MenuInput& in, float dt);
  void draw(int w, int h, float anim);

  // The colour of titles, the selection and switches (the running game's).
  void setAccent(ColorF c) { accent_ = c; }
  // The menus' look: 0 Storybook (bright and chunky), 1 Retro (black, bricks,
  // a pixel font), 2 Themed Dark (the classic panel with the game's heading),
  // 3 Classic. And the game the heading shows (Themed Dark): 0 the
  // collection, 1 Super Mario Bros., 2 The Lost Levels, 3 SMB2, 4 SMB3.
  enum Style { kStorybook, kRetro, kThemedDark, kClassic, kStyleCount };
  int style = kStorybook;
  void setGameArt(int g) { gameArt_ = g; }

  // OSD notifications (visible whether or not the menu is open)
  void notify(const std::string& text, float seconds = 2.2f, bool hint = false);
  void tickToasts(float dt);  // call every frame, menu open or not
  // "Achievement unlocked": a gold card that slides down at the top; several queue up.
  void achievement(const std::string& name, const std::string& desc);
  bool bannerShowing() const { return !banners_.empty(); }
  // `status`: what the emulation is doing besides running (rewinding...), top left.
  void drawOsd(int w, int h, const std::string& fps, const std::string& status = std::string());

  // Drawing helpers for custom pages
  void text(const std::wstring& s, RectF r, float size, bool bold, ColorF c,
            TextAlign align = kAlignLeading,
            ParaAlign palign = kParaCenter);
  void text(const std::wstring& s, RectF r, float size, int weight, ColorF c,
            TextAlign align = kAlignLeading,
            ParaAlign palign = kParaCenter);
  float measure(const std::wstring& s, float size, bool bold);  // width in pixels
  float measure(const std::wstring& s, float size, int weight);
  void fillRound(RectF r, float radius, ColorF c);
  void outlineRound(RectF r, float radius, float width, ColorF c);
  Canvas* canvas() const { return cv_; }
  float uiScale() const { return scale_; }
  // (Retro: text in the 5x7 pixel font, upper case)
  void pixelText(const std::wstring& s, RectF r, float size, ColorF c, TextAlign align, ParaAlign palign);
  float pixelMeasure(const std::wstring& s, float size);
  // A title or heading in the style's display face (Luckiest Guy, outlined; or pixels).
  void displayText(const std::wstring& s, RectF r, float size, ColorF fill, ColorF edge, float edgeW,
                   TextAlign align = kAlignLeading);
  float displayMeasure(const std::wstring& s, float size);
  void star(float cx, float cy, float r, ColorF fill, ColorF edge, float edgeW) { drawStar(cx, cy, r, fill, edge, edgeW); }
  ColorF accent() const { return accent_; }
  static std::wstring widen(const std::string& s);

private:
  Canvas* cv_ = nullptr;
  int gameArt_ = 0;
  std::vector<std::shared_ptr<MenuPage>> stack_;
  float scale_ = 1.0f;
  int lastW_ = 0, lastH_ = 0;  // (the screen as last drawn: wrapText)
  float safe_[4] = {0, 0, 0, 0};
  float rowText_ = 34, rowTouch_ = 34;  // (the rows' heights when last drawn)
  RectF listRect_{};                    // (where the list was)
  float rowHeight(const MenuItem& it) const;
  std::vector<float> rowTops(const MenuPage& p) const;  // each row's top from the list's start, and the end
  float time_ = 0;  // for the selection's pulse
  ColorF accent_ = {0.91f, 0.72f, 0.29f, 1.0f};
  // key repeat
  float repeatTimer_ = 0;
  int repeatDir_ = 0;
  struct Toast {
    std::string text;
    float time;
    float total;
    bool hint = false;  // removed as soon as the menu opens
  };
  std::vector<Toast> toasts_;
  struct Banner {
    std::string name, desc;
    float t = 0;
  };
  std::vector<Banner> banners_;
  void roundRect(RectF r, float radius, ColorF c);
  void drawToasts(int w, int h, float s);
  void drawBanner(int w, int h, float s);
  void drawStar(float cx, float cy, float r, ColorF fill, ColorF edge, float edgeW);
  void drawHeaderArt(RectF r, float a);  // (Themed Dark: the game's pattern behind the title)
  void drawBricks(RectF r, ColorF brick, ColorF mortar, float bh, float a);
  void activate(MenuItem& it);
  void adjust(MenuItem& it, int dir);
  bool selectable(const MenuItem& it) const;
  void moveSel(int dir);
  // (where the last draw put each row, the close button)
  std::vector<RowHit> rowHits_;
  RectF closeHit_{};
};
