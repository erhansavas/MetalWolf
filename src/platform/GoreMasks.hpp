// SPDX-License-Identifier: GPL-2.0-only
// Stain density masks shared by both gore renderers: 0 is clear, 1..255 is
// film thickness, MaskSize x MaskSize texels.
#pragma once
#include <cstdint>

namespace gore {
constexpr int MaskSize=128, PoolShapes=6, SplatShapes=8;
const uint8_t *poolMask(int shape);
const uint8_t *splatMask(int shape);
// Footprints: a right sole, heel at column 0 and toe at the last column,
// the inner (medial) side in the upper rows. A left print mirrors the rows.
// Texels hold how readily that part of the sole transfers blood (1..255):
// with little blood left only the highest-pressure contact points print.
enum PrintKind { PrintBoot, PrintClaw, PrintHoof, PrintPad, PrintPaw, PrintShapes };
const uint8_t *printMask(int kind);
uint32_t hash(uint32_t value);
}
