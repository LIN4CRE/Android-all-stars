// The 2D drawing the hub, the menus and the music player use: filled and
// outlined shapes, text in the system's UI font, and pictures. Implemented
// for Android in platform/android/video_gles.cpp (OpenGL ES, text from
// Android's own font engine); the host tests have one that records what is
// drawn (tests/host/fake_platform.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ColorF {
  float r = 0, g = 0, b = 0, a = 1;
};
struct RectF {
  float left = 0, top = 0, right = 0, bottom = 0;
};
struct PointF {
  float x = 0, y = 0;
};
struct IRect {  // (in pixels: the game picture's place on the screen)
  int left = 0, top = 0, right = 0, bottom = 0;
};
inline ColorF colorF(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
inline RectF rectF(float l, float t, float r, float b) { return {l, t, r, b}; }

enum TextAlign { kAlignLeading, kAlignTrailing, kAlignCenter };
enum ParaAlign { kParaNear, kParaFar, kParaCenter };
enum FontWeight { kWeightNormal = 400, kWeightSemiBold = 600, kWeightBold = 700 };

class Canvas {
public:
  virtual ~Canvas() = default;
  // Shapes and images belong to the canvas that made them: a new one (the
  // graphics device was rebuilt) has another epoch, and they're made again.
  virtual unsigned epoch() const = 0;
  virtual void fillRect(RectF r, ColorF c) = 0;
  virtual void strokeRect(RectF r, float width, ColorF c) = 0;
  virtual void fillRound(RectF r, float radius, ColorF c) = 0;
  virtual void strokeRound(RectF r, float radius, float width, ColorF c) = 0;
  virtual void strokeEllipse(float cx, float cy, float rx, float ry, float width, ColorF c) = 0;
  // Top to bottom.
  virtual void fillGradient(RectF r, ColorF top, ColorF bottom) = 0;
  // A closed polygon, each star-shaped about the mean of its points (a star,
  // a quad); filled, or outlined.
  virtual void fillPolygon(const PointF* pts, int n, ColorF c) = 0;
  virtual void strokePolygon(const PointF* pts, int n, float width, ColorF c) = 0;
  // Many polygons kept for drawing again: `counts` points each, one after
  // another in `pts`. Drawn
  // scaled by `scale` about the origin, then moved by (tx, ty).
  virtual int makeShape(const std::vector<PointF>& pts, const std::vector<int>& counts) = 0;
  virtual void fillShape(int shape, ColorF c, float scale = 1.0f, float tx = 0, float ty = 0) = 0;
  virtual void freeShape(int shape) = 0;

  // Single-line text, clipped to `r`, aligned in it.
  virtual void text(const std::wstring& s, RectF r, float size, int weight, ColorF c, TextAlign align = kAlignLeading,
                    ParaAlign para = kParaCenter) = 0;
  virtual float measure(const std::wstring& s, float size, int weight) = 0;  // width in pixels
  // ... in the title font (Luckiest Guy, third_party/fonts); the UI font,
  // bold, where it isn't found.
  virtual void titleText(const std::wstring& s, RectF r, float size, ColorF c, TextAlign align = kAlignLeading,
                         ParaAlign para = kParaCenter) = 0;
  virtual float measureTitle(const std::wstring& s, float size) = 0;

  virtual void pushClip(RectF r) = 0;
  virtual void popClip() = 0;

  // Pictures (0x00RRGGBB pixels; with `alpha`, 0xAARRGGBB, not premultiplied),
  // drawn stretched into `dst`.
  virtual int makeImage(int w, int h, bool alpha = false) = 0;
  virtual void updateImage(int image, const uint32_t* px, int stridePixels) = 0;
  virtual void drawImage(int image, RectF dst, float opacity, bool smooth) = 0;
  virtual void freeImage(int image) = 0;
};

// UTF-8 to and from wide strings (wchar_t is 32 bits on Android and Linux).
std::wstring widenUtf8(const std::string& s);
std::string narrowUtf8(const std::wstring& w);
