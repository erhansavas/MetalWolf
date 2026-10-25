// SPDX-License-Identifier: GPL-2.0-only
// Synthesized gore sounds: each event is a premixed stack of layers, so one
// hit costs one voice. Nothing is recorded or downloaded; the only source
// material is the WAD's own DSSLOP for the wet squelch layer.
#pragma once
#include <cstddef>
#include <vector>

namespace gore_sound {
enum Kind { Hit, Heavy, Kill, Gib, Slap, Spurt, Drip, Kinds };
constexpr int Variants=4;
constexpr unsigned Rate=44100;
// slop: DSSLOP PCM at slopRate (count may be 0: the squelch is synthesized).
std::vector<float> build(Kind kind,int variant,const float *slop,size_t count,unsigned slopRate);
}
