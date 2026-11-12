// Character colour variants: re-hue the clothing colours of a 16-colour
// sprite palette while keeping outlines, skin and hair untouched.
#pragma once
#include <cstdint>

// variant: 0 = original (copy), 1.. = see kVariantNames in palette.cpp
void recolorCharacterPalette(const uint16_t* src, uint16_t* dst, int variant);
int characterVariantCount();
