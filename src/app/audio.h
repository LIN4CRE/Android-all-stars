// Sound output. On Android it is SDL's audio queue, with resampling from the
// console's 32040 Hz to the device's rate and a gentle rate control that keeps
// the latency steady: platform/android/audio_sdl.h.
#pragma once
#include "platform/android/audio_sdl.h"
