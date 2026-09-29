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
 * ITU-T H.273 ColourPrimaries / TransferCharacteristics → GCOL_Color_Info.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/cicp.h>

/** H.273 Table 2 (ColourPrimaries) → a gamut this library names. */
static GCOL_Primaries primaries_from_h273(unsigned code) {
  switch (code) {
  case 1u: /* BT.709 / sRGB. */
    return GCOL_PRIMARIES_SRGB;
  case 9u: /* BT.2020 / BT.2100. */
    return GCOL_PRIMARIES_BT2020;
  case 12u: /* SMPTE EG 432-1, P3-D65 — Display P3. */
    return GCOL_PRIMARIES_DISPLAY_P3;
  default:
    return GCOL_PRIMARIES_UNKNOWN;
  }
}

/**
 * H.273 Table 3 (TransferCharacteristics) into @p out.
 *
 * Code points 1, 6, 14 and 15 are BT.709's curve at different bit depths,
 * stored as ICC parametricCurveType type 3 terms.
 */
static bool transfer_from_h273(unsigned code, GCOL_Color_Info * out) {
  switch (code) {
  case 1u:
  case 6u:
  case 14u:
  case 15u:
    /* L = ((V + 0.099) / 1.099)^(1/0.45) for V >= 0.081, else V / 4.5. */
    out->transfer = GCOL_TRANSFER_PARAMETRIC;
    out->transfer_params[0] = 1.0 / 0.45;    /* g */
    out->transfer_params[1] = 1.0 / 1.099;   /* a */
    out->transfer_params[2] = 0.099 / 1.099; /* b */
    out->transfer_params[3] = 1.0 / 4.5;     /* c */
    out->transfer_params[4] = 0.081;         /* d */
    break;
  case 4u: /* BT.470 System M. */
    out->transfer = GCOL_TRANSFER_GAMMA;
    out->gamma_value = 2.2;
    break;
  case 5u: /* BT.470 System B/G. */
    out->transfer = GCOL_TRANSFER_GAMMA;
    out->gamma_value = 2.8;
    break;
  case 8u:
    out->transfer = GCOL_TRANSFER_LINEAR;
    break;
  case 13u:
    out->transfer = GCOL_TRANSFER_SRGB;
    break;
  case 16u:
    out->transfer = GCOL_TRANSFER_PQ;
    break;
  case 18u:
    out->transfer = GCOL_TRANSFER_HLG;
    break;
  default:
    return false;
  }
  (void)gcol_transfer_conventions(out->transfer, &out->reference,
      &out->sample_scale, &out->white_luminance);
  return true;
}

GCOL_API bool gcol_cicp_to_color_info(unsigned colour_primaries,
    unsigned transfer_characteristics, GCOL_Color_Info * out) {
  if (!out) {
    return false;
  }
  bool said = false;
  const GCOL_Primaries named = primaries_from_h273(colour_primaries);
  if (named != GCOL_PRIMARIES_UNKNOWN) {
    said = gcol_color_info_set_gamut(out, named);
  }
  if (transfer_from_h273(transfer_characteristics, out)) {
    said = true;
  }
  return said;
}
