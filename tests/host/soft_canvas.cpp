// A software Canvas for the host: see soft_canvas.h.
#include "soft_canvas.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct SoftCanvas::Font {
  std::vector<unsigned char> data;
  stbtt_fontinfo info{};
  bool ok = false;
  int ascent = 0, descent = 0, lineGap = 0;
  explicit Font(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
      std::fseek(f, 0, SEEK_END);
      const long n = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      if (n > 0) {
        data.resize((size_t)n);
        if (std::fread(data.data(), 1, (size_t)n, f) != (size_t)n) data.clear();
      }
      std::fclose(f);
    }
    if (!data.empty() && stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) {
      ok = true;
      stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    }
  }
  // (`size` is the em height in pixels, as Android's Paint.setTextSize)
  float scale(float size) const { return stbtt_ScaleForMappingEmToPixels(&info, size); }
};

SoftCanvas::SoftCanvas(int w, int h, const std::string& body, const std::string& bold, const std::string& title)
    : w_(w), h_(h), px_((size_t)w * h * 3, 0.0f) {
  body_ = std::make_unique<Font>(body);
  bold_ = std::make_unique<Font>(bold);
  title_ = std::make_unique<Font>(title);
}
SoftCanvas::~SoftCanvas() = default;

bool SoftCanvas::fontsOk() const { return body_->ok && bold_->ok && title_->ok; }

void SoftCanvas::clear(ColorF c) {
  for (size_t i = 0; i < px_.size(); i += 3) px_[i] = c.r, px_[i + 1] = c.g, px_[i + 2] = c.b;
  clips_.clear();
}

bool SoftCanvas::savePpm(const std::string& path, int shrink) const {
  shrink = std::max(1, shrink);
  const int ow = w_ / shrink, oh = h_ / shrink;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", ow, oh);
  std::vector<unsigned char> row((size_t)ow * 3);
  for (int y = 0; y < oh; y++) {
    for (int x = 0; x < ow; x++) {
      float acc[3] = {0, 0, 0};
      for (int sy = 0; sy < shrink; sy++)
        for (int sx = 0; sx < shrink; sx++) {
          const float* p = &px_[((size_t)(y * shrink + sy) * w_ + (x * shrink + sx)) * 3];
          acc[0] += p[0], acc[1] += p[1], acc[2] += p[2];
        }
      for (int k = 0; k < 3; k++)
        row[(size_t)x * 3 + k] = (unsigned char)std::lround(std::clamp(acc[k] / (float)(shrink * shrink), 0.0f, 1.0f) * 255.0f);
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  return std::fclose(f) == 0;
}

RectF SoftCanvas::clip() const {
  RectF c = rectF(0, 0, (float)w_, (float)h_);
  for (const RectF& r : clips_) {
    c.left = std::max(c.left, r.left);
    c.top = std::max(c.top, r.top);
    c.right = std::min(c.right, r.right);
    c.bottom = std::min(c.bottom, r.bottom);
  }
  return c;
}

void SoftCanvas::pushClip(RectF r) { clips_.push_back(r); }
void SoftCanvas::popClip() {
  if (!clips_.empty()) clips_.pop_back();
}

void SoftCanvas::blend(int x, int y, ColorF c, float coverage) {
  const float a = std::clamp(c.a * coverage, 0.0f, 1.0f);
  if (a <= 0) return;
  float* p = &px_[((size_t)y * w_ + x) * 3];
  p[0] += (c.r - p[0]) * a;
  p[1] += (c.g - p[1]) * a;
  p[2] += (c.b - p[2]) * a;
}

// Every pixel of `bounds` (inside the clip): covered by how far inside the
// shape its centre is (`dist`: negative inside, in pixels).
template <typename Dist>
void SoftCanvas::paint(RectF bounds, ColorF c, Dist dist) {
  if (!painting) return;
  const RectF cl = clip();
  const int x0 = std::max((int)std::floor(std::max(bounds.left, cl.left)), 0);
  const int y0 = std::max((int)std::floor(std::max(bounds.top, cl.top)), 0);
  const int x1 = std::min((int)std::ceil(std::min(bounds.right, cl.right)), w_);
  const int y1 = std::min((int)std::ceil(std::min(bounds.bottom, cl.bottom)), h_);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      const float px = (float)x + 0.5f, py = (float)y + 0.5f;
      if (px < cl.left || px >= cl.right || py < cl.top || py >= cl.bottom) continue;
      const float cov = std::clamp(0.5f - dist(px, py), 0.0f, 1.0f);
      if (cov > 0) blend(x, y, c, cov);
    }
}

namespace {
float roundDist(const RectF& r, float rad, float x, float y) {
  const float hw = (r.right - r.left) * 0.5f, hh = (r.bottom - r.top) * 0.5f;
  rad = std::clamp(rad, 0.0f, std::min(hw, hh));
  const float qx = std::fabs(x - (r.left + r.right) * 0.5f) - (hw - rad), qy = std::fabs(y - (r.top + r.bottom) * 0.5f) - (hh - rad);
  return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - rad;
}
float segDist(PointF a, PointF b, float x, float y) {
  const float vx = b.x - a.x, vy = b.y - a.y, wx = x - a.x, wy = y - a.y;
  const float len2 = vx * vx + vy * vy, t = len2 > 0 ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
  return std::hypot(wx - vx * t, wy - vy * t);
}
bool inPoly(const PointF* p, int n, float x, float y) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++)
    if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
  return in;
}
RectF grow(RectF r, float by) { return rectF(r.left - by, r.top - by, r.right + by, r.bottom + by); }
}  // namespace

void SoftCanvas::fillRect(RectF r, ColorF c) {
  paint(grow(r, 1), c, [&](float x, float y) { return roundDist(r, 0, x, y); });
}
void SoftCanvas::strokeRect(RectF r, float width, ColorF c) {
  paint(grow(r, width + 1), c, [&](float x, float y) { return std::fabs(roundDist(r, 0, x, y)) - width * 0.5f; });
}
void SoftCanvas::fillRound(RectF r, float radius, ColorF c) {
  paint(grow(r, 1), c, [&](float x, float y) { return roundDist(r, radius, x, y); });
}
void SoftCanvas::strokeRound(RectF r, float radius, float width, ColorF c) {
  paint(grow(r, width + 1), c, [&](float x, float y) { return std::fabs(roundDist(r, radius, x, y)) - width * 0.5f; });
}
void SoftCanvas::strokeEllipse(float cx, float cy, float rx, float ry, float width, ColorF c) {
  const RectF b = rectF(cx - rx, cy - ry, cx + rx, cy + ry);
  paint(grow(b, width + 1), c, [&](float x, float y) {
    const float k = std::hypot((x - cx) / std::max(rx, 0.001f), (y - cy) / std::max(ry, 0.001f));
    return std::fabs((k - 1.0f) * std::min(rx, ry)) - width * 0.5f;
  });
}
void SoftCanvas::fillGradient(RectF r, ColorF top, ColorF bottom) {
  if (!painting) return;
  const RectF cl = clip();
  const int x0 = std::max((int)std::floor(std::max(r.left, cl.left)), 0), x1 = std::min((int)std::ceil(std::min(r.right, cl.right)), w_);
  const int y0 = std::max((int)std::floor(std::max(r.top, cl.top)), 0), y1 = std::min((int)std::ceil(std::min(r.bottom, cl.bottom)), h_);
  for (int y = y0; y < y1; y++) {
    const float t = std::clamp(((float)y + 0.5f - r.top) / std::max(1.0f, r.bottom - r.top), 0.0f, 1.0f);
    const ColorF c = colorF(top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t,
                            top.a + (bottom.a - top.a) * t);
    for (int x = x0; x < x1; x++) blend(x, y, c, 1.0f);
  }
}
void SoftCanvas::fillPolygon(const PointF* pts, int n, ColorF c) {
  if (n < 3) return;
  RectF b = rectF(1e9f, 1e9f, -1e9f, -1e9f);
  for (int i = 0; i < n; i++) {
    b.left = std::min(b.left, pts[i].x), b.right = std::max(b.right, pts[i].x);
    b.top = std::min(b.top, pts[i].y), b.bottom = std::max(b.bottom, pts[i].y);
  }
  paint(grow(b, 1), c, [&](float x, float y) {
    // (4 x 4 samples in the pixel)
    int hit = 0;
    for (int sy = 0; sy < 4; sy++)
      for (int sx = 0; sx < 4; sx++) hit += inPoly(pts, n, x - 0.375f + (float)sx * 0.25f, y - 0.375f + (float)sy * 0.25f) ? 1 : 0;
    return 0.5f - (float)hit / 16.0f;
  });
}
void SoftCanvas::strokePolygon(const PointF* pts, int n, float width, ColorF c) {
  if (n < 2) return;
  RectF b = rectF(1e9f, 1e9f, -1e9f, -1e9f);
  for (int i = 0; i < n; i++) {
    b.left = std::min(b.left, pts[i].x), b.right = std::max(b.right, pts[i].x);
    b.top = std::min(b.top, pts[i].y), b.bottom = std::max(b.bottom, pts[i].y);
  }
  paint(grow(b, width + 1), c, [&](float x, float y) {
    float d = 1e9f;
    for (int i = 0, j = n - 1; i < n; j = i++) d = std::min(d, segDist(pts[j], pts[i], x, y));
    return d - width * 0.5f;
  });
}
int SoftCanvas::makeShape(const std::vector<PointF>& pts, const std::vector<int>& counts) {
  shapes_.push_back({pts, counts});
  return (int)shapes_.size();
}
void SoftCanvas::fillShape(int shape, ColorF c, float scale, float tx, float ty) {
  if (shape < 1 || shape > (int)shapes_.size()) return;
  const Shape& s = shapes_[(size_t)shape - 1];
  size_t at = 0;
  for (int n : s.counts) {
    std::vector<PointF> p((size_t)n);
    for (int i = 0; i < n; i++) p[(size_t)i] = {s.pts[at + i].x * scale + tx, s.pts[at + i].y * scale + ty};
    fillPolygon(p.data(), n, c);
    at += (size_t)n;
  }
}

float SoftCanvas::measureText(Font* f, const std::wstring& s, float size) {
  if (!f->ok) return (float)s.size() * size * 0.55f;
  const float sc = f->scale(size);
  float w = 0;
  for (size_t i = 0; i < s.size(); i++) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f->info, (int)s[i], &adv, &lsb);
    w += (float)adv * sc;
    if (i + 1 < s.size()) w += (float)stbtt_GetCodepointKernAdvance(&f->info, (int)s[i], (int)s[i + 1]) * sc;
  }
  return w;
}

void SoftCanvas::drawText(Font* f, const std::wstring& s, RectF r, float size, ColorF c, TextAlign align, ParaAlign para) {
  if (!painting || !f->ok || s.empty() || size < 1) return;
  const float sc = f->scale(size), w = measureText(f, s, size);
  const float asc = (float)f->ascent * sc, desc = (float)-f->descent * sc, lineH = asc + desc;
  float x = align == kAlignLeading ? r.left : align == kAlignTrailing ? r.right - w : (r.left + r.right - w) * 0.5f;
  const float top = para == kParaNear ? r.top : para == kParaFar ? r.bottom - lineH : (r.top + r.bottom - lineH) * 0.5f;
  const float base = top + asc;
  pushClip(r);
  const RectF cl = clip();
  std::vector<unsigned char> bmp;
  for (size_t i = 0; i < s.size(); i++) {
    const int cp = (int)s[i];
    int adv = 0, lsb = 0, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    const float shift = x - std::floor(x);
    stbtt_GetCodepointBitmapBoxSubpixel(&f->info, cp, sc, sc, shift, 0, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      bmp.assign((size_t)gw * gh, 0);
      stbtt_MakeCodepointBitmapSubpixel(&f->info, bmp.data(), gw, gh, gw, sc, sc, shift, 0, cp);
      const int ox = (int)std::floor(x) + x0, oy = (int)std::lround(base) + y0;
      for (int gy = 0; gy < gh; gy++) {
        const int py = oy + gy;
        if (py < 0 || py >= h_ || (float)py + 0.5f < cl.top || (float)py + 0.5f >= cl.bottom) continue;
        for (int gx = 0; gx < gw; gx++) {
          const int pxx = ox + gx;
          if (pxx < 0 || pxx >= w_ || (float)pxx + 0.5f < cl.left || (float)pxx + 0.5f >= cl.right) continue;
          const unsigned char v = bmp[(size_t)gy * gw + gx];
          if (v) blend(pxx, py, c, (float)v / 255.0f);
        }
      }
    }
    x += (float)adv * sc;
    if (i + 1 < s.size()) x += (float)stbtt_GetCodepointKernAdvance(&f->info, cp, (int)s[i + 1]) * sc;
  }
  popClip();
}

void SoftCanvas::text(const std::wstring& s, RectF r, float size, int weight, ColorF c, TextAlign align, ParaAlign para) {
  drawText(weight >= kWeightSemiBold ? bold_.get() : body_.get(), s, r, size, c, align, para);
}
float SoftCanvas::measure(const std::wstring& s, float size, int weight) {
  return measureText(weight >= kWeightSemiBold ? bold_.get() : body_.get(), s, size);
}
void SoftCanvas::titleText(const std::wstring& s, RectF r, float size, ColorF c, TextAlign align, ParaAlign para) {
  drawText(title_.get(), s, r, size, c, align, para);
}
float SoftCanvas::measureTitle(const std::wstring& s, float size) { return measureText(title_.get(), s, size); }

int SoftCanvas::makeImage(int w, int h, bool alpha) {
  Image im;
  im.w = w, im.h = h, im.alpha = alpha;
  im.px.assign((size_t)std::max(0, w) * std::max(0, h), 0);
  images_.push_back(std::move(im));
  return (int)images_.size();
}
void SoftCanvas::updateImage(int image, const uint32_t* px, int stridePixels) {
  if (image < 1 || image > (int)images_.size() || !px) return;
  Image& im = images_[(size_t)image - 1];
  for (int y = 0; y < im.h; y++) std::memcpy(&im.px[(size_t)y * im.w], px + (size_t)y * stridePixels, (size_t)im.w * 4);
}
void SoftCanvas::drawImage(int image, RectF dst, float opacity, bool) {
  if (image < 1 || image > (int)images_.size()) return;
  const Image& im = images_[(size_t)image - 1];
  if (!painting || im.w <= 0 || im.h <= 0) return;
  const RectF cl = clip();
  const int x0 = std::max((int)std::floor(std::max(dst.left, cl.left)), 0), x1 = std::min((int)std::ceil(std::min(dst.right, cl.right)), w_);
  const int y0 = std::max((int)std::floor(std::max(dst.top, cl.top)), 0), y1 = std::min((int)std::ceil(std::min(dst.bottom, cl.bottom)), h_);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      const int sx = std::clamp((int)(((float)x + 0.5f - dst.left) / (dst.right - dst.left) * (float)im.w), 0, im.w - 1);
      const int sy = std::clamp((int)(((float)y + 0.5f - dst.top) / (dst.bottom - dst.top) * (float)im.h), 0, im.h - 1);
      const uint32_t v = im.px[(size_t)sy * im.w + sx];
      const float a = im.alpha ? (float)(v >> 24) / 255.0f : 1.0f;
      blend(x, y, colorF((float)((v >> 16) & 255) / 255.0f, (float)((v >> 8) & 255) / 255.0f, (float)(v & 255) / 255.0f, a), opacity);
    }
}
