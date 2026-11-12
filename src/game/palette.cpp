#include "game/palette.h"
#include <algorithm>
#include <cmath>

namespace {

struct Target {
  float hue;   // degrees
  float sMul;  // saturation multiplier
  float vMul;  // value multiplier
};
struct Variant {
  Target primary;    // cap / shirt (red or green in the originals)
  Target secondary;  // overalls (blue in the originals)
};

// 1 Classic NES, 2 Fire, 3 Ice, 4 Shadow, 5 Gold, 6 Forest, 7 Ocean
const Variant kVariants[] = {
    {{0, 1.0f, 1.0f}, {0, 1.0f, 1.0f}},           // 0: unused (original)
    {{2, 1.0f, 1.0f}, {26, 0.95f, 0.55f}},        // red shirt, brown overalls
    {{0, 0.06f, 1.35f}, {2, 1.05f, 1.0f}},        // white shirt, red overalls
    {{195, 0.55f, 1.25f}, {205, 0.12f, 1.3f}},    // light blue, white
    {{278, 0.6f, 0.62f}, {250, 0.45f, 0.35f}},    // purple, near black
    {{46, 0.95f, 1.2f}, {30, 0.95f, 0.8f}},       // gold, amber
    {{118, 0.8f, 0.9f}, {30, 0.75f, 0.6f}},       // green, brown
    {{176, 0.85f, 0.95f}, {226, 0.95f, 0.6f}},    // teal, navy
};

void toHsv(float r, float g, float b, float& h, float& s, float& v) {
  float mx = std::max({r, g, b}), mn = std::min({r, g, b});
  v = mx;
  float d = mx - mn;
  s = mx > 0 ? d / mx : 0;
  if (d <= 0) {
    h = 0;
    return;
  }
  if (mx == r) h = 60 * std::fmod((g - b) / d, 6.0f);
  else if (mx == g) h = 60 * ((b - r) / d + 2);
  else h = 60 * ((r - g) / d + 4);
  if (h < 0) h += 360;
}

void fromHsv(float h, float s, float v, float& r, float& g, float& b) {
  float c = v * s;
  float hp = std::fmod(h, 360.0f) / 60.0f;
  float x = c * (1 - std::fabs(std::fmod(hp, 2.0f) - 1));
  float r1 = 0, g1 = 0, b1 = 0;
  if (hp < 1) { r1 = c; g1 = x; }
  else if (hp < 2) { r1 = x; g1 = c; }
  else if (hp < 3) { g1 = c; b1 = x; }
  else if (hp < 4) { g1 = x; b1 = c; }
  else if (hp < 5) { r1 = x; b1 = c; }
  else { r1 = c; b1 = x; }
  float m = v - c;
  r = r1 + m;
  g = g1 + m;
  b = b1 + m;
}

}  // namespace

int characterVariantCount() { return (int)(sizeof(kVariants) / sizeof(kVariants[0])); }

void recolorCharacterPalette(const uint16_t* src, uint16_t* dst, int variant) {
  for (int i = 0; i < 16; i++) dst[i] = src[i];
  if (variant <= 0 || variant >= characterVariantCount()) return;
  const Variant& vt = kVariants[variant];
  for (int i = 1; i < 16; i++) {
    uint16_t c = src[i];
    float r = (c & 31) / 31.0f, g = ((c >> 5) & 31) / 31.0f, b = ((c >> 10) & 31) / 31.0f;
    float h, s, v;
    toHsv(r, g, b, h, s, v);
    if (s < 0.22f || v < 0.16f) continue;           // outlines, whites, greys
    if (h >= 10 && h < 55) continue;                // skin, hair, shoes
    // (the skin's shading: a pale salmon, red in hue but far less saturated
    // than the cap and shirt; recoloured, the face took the suit's colour)
    if ((h >= 340 || h < 10) && s < 0.75f && v > 0.55f) continue;
    const Target& t = (h >= 170 && h < 300) ? vt.secondary : vt.primary;
    float ns = std::clamp(s * t.sMul, 0.0f, 1.0f);
    float nv = std::clamp(v * t.vMul, 0.0f, 1.0f);
    fromHsv(t.hue, ns, nv, r, g, b);
    int ri = std::clamp((int)std::lround(r * 31), 0, 31);
    int gi = std::clamp((int)std::lround(g * 31), 0, 31);
    int bi = std::clamp((int)std::lround(b * 31), 0, 31);
    dst[i] = (uint16_t)(ri | (gi << 5) | (bi << 10));
  }
}
