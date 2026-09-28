/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Color.
 *
 * Ghoti.io Color is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Color is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Named gamuts and colour-info defaults.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/color.h>
#include <string.h>

/** A named gamut and the coordinates that identify it. */
typedef struct {
  GCOL_Primaries named;
  GCOL_Gamut gamut;
} gcol_named_gamut_t;

/**
 * The spaces this library can put a name to.
 *
 * White points are the standard illuminants: D65 is (0.3127, 0.3290) and D50
 * is (0.3457, 0.3585), both as CIE 1931 2-degree observer chromaticities.
 *
 * No two entries share their three primaries, which is what lets
 * gcol_gamut_identify() name a gamut that carries no white point.  Adding a
 * space that breaks that - theatrical DCI-P3, whose primaries are Display
 * P3's and whose white is not D65 - means teaching that function to refuse
 * rather than guess, and the assertion in the unit tests is there to make the
 * addition fail loudly rather than silently pick one.
 */
static const gcol_named_gamut_t gcol_named_gamuts[] = {
    // IEC 61966-2.1 (sRGB), sharing ITU-R BT.709's primaries.
    {GCOL_PRIMARIES_SRGB,
        {{0.3127, 0.3290}, {0.6400, 0.3300}, {0.3000, 0.6000},
            {0.1500, 0.0600}}},
    // Adobe RGB (1998): BT.709's red and blue, a wider green.
    {GCOL_PRIMARIES_ADOBE_RGB,
        {{0.3127, 0.3290}, {0.6400, 0.3300}, {0.2100, 0.7100},
            {0.1500, 0.0600}}},
    // Display P3: SMPTE RP 431-2 primaries on a D65 white.
    {GCOL_PRIMARIES_DISPLAY_P3,
        {{0.3127, 0.3290}, {0.6800, 0.3200}, {0.2650, 0.6900},
            {0.1500, 0.0600}}},
    // ITU-R BT.2020.
    {GCOL_PRIMARIES_BT2020,
        {{0.3127, 0.3290}, {0.7080, 0.2920}, {0.1700, 0.7970},
            {0.1310, 0.0460}}},
    // ROMM RGB (ProPhoto), the one here on a D50 white.
    {GCOL_PRIMARIES_PROPHOTO,
        {{0.3457, 0.3585}, {0.7347, 0.2653}, {0.1596, 0.8404},
            {0.0366, 0.0001}}},
};

/** True when two chromaticities agree to @p tolerance on both axes. */
static bool gcol_chroma_close(const GCOL_Chromaticity * a,
    const GCOL_Chromaticity * b, double tolerance) {
  double dx = a->x - b->x;
  double dy = a->y - b->y;
  if (dx < 0) {
    dx = -dx;
  }
  if (dy < 0) {
    dy = -dy;
  }
  // Written so that a NaN fails: GCOL_Color_Info carries doubles that nothing
  // polices, and a NaN compares false against every bound rather than true.
  return dx <= tolerance && dy <= tolerance;
}

/** True when @p c is the unset {0, 0}. */
static bool gcol_chroma_unset(const GCOL_Chromaticity * c) {
  return c->x == 0.0 && c->y == 0.0;
}

GCOL_API GCOL_Primaries gcol_gamut_identify(
    const GCOL_Gamut * gamut, double tolerance) {
  if (!gamut) {
    return GCOL_PRIMARIES_UNKNOWN;
  }
  if (!(tolerance > 0.0)) {
    // Covers a NaN as well as zero and negatives.
    tolerance = GCOL_GAMUT_TOLERANCE_DEFAULT;
  }
  if (gcol_chroma_unset(&gamut->red) && gcol_chroma_unset(&gamut->green) &&
      gcol_chroma_unset(&gamut->blue)) {
    // No primaries is not a gamut.  The coordinates would not have matched
    // anything anyway; saying so here means a caller reading this function
    // does not have to work that out from the table.
    return GCOL_PRIMARIES_UNKNOWN;
  }
  const bool have_white = !gcol_chroma_unset(&gamut->white);
  for (size_t i = 0; i < GCOL_ARRAY_SIZE(gcol_named_gamuts); i++) {
    const GCOL_Gamut * known = &gcol_named_gamuts[i].gamut;
    if (!gcol_chroma_close(&gamut->red, &known->red, tolerance) ||
        !gcol_chroma_close(&gamut->green, &known->green, tolerance) ||
        !gcol_chroma_close(&gamut->blue, &known->blue, tolerance)) {
      continue;
    }
    // A file that states no white point - a BMP V4 header has nowhere to put
    // one - is matched on its primaries, which is unambiguous only because no
    // two entries above share theirs.
    if (have_white &&
        !gcol_chroma_close(&gamut->white, &known->white, tolerance)) {
      continue;
    }
    return gcol_named_gamuts[i].named;
  }
  return GCOL_PRIMARIES_UNKNOWN;
}

GCOL_API bool gcol_gamut_named(GCOL_Primaries named, GCOL_Gamut * out_gamut) {
  if (!out_gamut) {
    return false;
  }
  for (size_t i = 0; i < GCOL_ARRAY_SIZE(gcol_named_gamuts); i++) {
    if (gcol_named_gamuts[i].named == named) {
      *out_gamut = gcol_named_gamuts[i].gamut;
      return true;
    }
  }
  return false;
}

GCOL_API bool gcol_color_info_set_gamut(
    GCOL_Color_Info * info, GCOL_Primaries named) {
  if (!info) {
    return false;
  }
  if (!gcol_gamut_named(named, &info->gamut)) {
    memset(&info->gamut, 0, sizeof(info->gamut));
    info->primaries_stated = false;
    info->white_stated = false;
    return false;
  }
  info->primaries_stated = true;
  info->white_stated = true;
  return true;
}

GCOL_API void gcol_color_info_default(GCOL_Color_Info * info) {
  if (!info) {
    return;
  }
  // memset rather than a designated initializer: every field added later is
  // then zeroed by construction instead of by remembering to name it here.
  memset(info, 0, sizeof(*info));
  info->transfer = GCOL_TRANSFER_UNKNOWN;
  info->reference = GCOL_REFERENCE_UNKNOWN;
  info->sample_scale = GCOL_SAMPLE_SCALE_UNKNOWN;
  info->intent = GCOL_INTENT_PERCEPTUAL;
  info->cmyk_polarity = GCOL_CMYK_POLARITY_UNKNOWN;
}
