// A software Canvas for the host: draws what the app draws (the hub, the
// menus, the on-screen controls) into a pixel buffer that can be saved as a
// picture, so the layout can be looked at without a phone. Used by the
// preview tool (tests/preview_main.cpp) only: the tests themselves use the
// recording canvas in fake_platform.cpp.
//
// Shapes are anti-aliased from their distance functions; text is rasterised
// with stb_truetype from fonts on the host (so letter shapes and widths are
// close to, not the same as, the phone's own font).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "app/canvas.h"

class SoftCanvas : public Canvas {
public:
  // `body` and `bold`: TrueType files for the text; `title`: the title font.
  SoftCanvas(int w, int h, const std::string& body, const std::string& bold, const std::string& title);
  ~SoftCanvas() override;
  bool fontsOk() const;
  void clear(ColorF c);
  // A binary PPM (P6), each `shrink` x `shrink` pixels averaged into one.
  bool savePpm(const std::string& path, int shrink = 1) const;
  int width() const { return w_; }
  int height() const { return h_; }
  // Off: nothing is painted (text is still measured). The preview tool lays
  // out many frames and paints only the last.
  bool painting = true;

  unsigned epoch() const override { return 1; }
  void fillRect(RectF r, ColorF c) override;
  void strokeRect(RectF r, float width, ColorF c) override;
  void fillRound(RectF r, float radius, ColorF c) override;
  void strokeRound(RectF r, float radius, float width, ColorF c) override;
  void strokeEllipse(float cx, float cy, float rx, float ry, float width, ColorF c) override;
  void fillGradient(RectF r, ColorF top, ColorF bottom) override;
  void fillPolygon(const PointF* pts, int n, ColorF c) override;
  void strokePolygon(const PointF* pts, int n, float width, ColorF c) override;
  int makeShape(const std::vector<PointF>& pts, const std::vector<int>& counts) override;
  void fillShape(int shape, ColorF c, float scale, float tx, float ty) override;
  void freeShape(int) override {}
  void text(const std::wstring& s, RectF r, float size, int weight, ColorF c, TextAlign align, ParaAlign para) override;
  float measure(const std::wstring& s, float size, int weight) override;
  void titleText(const std::wstring& s, RectF r, float size, ColorF c, TextAlign align, ParaAlign para) override;
  float measureTitle(const std::wstring& s, float size) override;
  void pushClip(RectF r) override;
  void popClip() override;
  int makeImage(int w, int h, bool alpha) override;
  void updateImage(int image, const uint32_t* px, int stridePixels) override;
  void drawImage(int image, RectF dst, float opacity, bool smooth) override;
  void freeImage(int) override {}

private:
  struct Font;
  struct Image {
    int w = 0, h = 0;
    bool alpha = false;
    std::vector<uint32_t> px;
  };
  struct Shape {
    std::vector<PointF> pts;
    std::vector<int> counts;
  };
  int w_, h_;
  std::vector<float> px_;  // r, g, b per pixel, 0..1
  std::vector<RectF> clips_;
  std::unique_ptr<Font> body_, bold_, title_;
  std::vector<Image> images_;
  std::vector<Shape> shapes_;

  RectF clip() const;
  void blend(int x, int y, ColorF c, float coverage);
  template <typename Dist>
  void paint(RectF bounds, ColorF c, Dist dist);
  void drawText(Font* f, const std::wstring& s, RectF r, float size, ColorF c, TextAlign align, ParaAlign para);
  float measureText(Font* f, const std::wstring& s, float size);
};
