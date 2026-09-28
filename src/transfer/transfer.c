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
 * Transfer-function conventions (reference, scale, peak luminance).
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/color.h>

/**
 * What each named transfer settles about the samples it encodes.
 *
 * ST 2084 (PQ) is defined as an absolute EOTF: a code value names a display
 * luminance in cd/m^2, and 10000 is the peak the curve is specified to.
 * BT.2100's HLG is an OETF over scene light and is relative - its system
 * gamma depends on the display's own peak, so no figure here would be right.
 * sRGB and BT.1886 describe a display and carry no absolute level.
 *
 * LINEAR, GAMMA and PARAMETRIC are absent on purpose: those curves say
 * nothing about what the samples are measured against, and guessing would be
 * the rounding this model exists to avoid.
 */
typedef struct {
  GCOL_Transfer transfer;
  GCOL_Reference reference;
  GCOL_Sample_Scale scale;
  double white_luminance;
} gcol_transfer_convention_t;

static const gcol_transfer_convention_t gcol_transfer_conventions_table[] = {
    {GCOL_TRANSFER_SRGB, GCOL_REFERENCE_DISPLAY, GCOL_SAMPLE_SCALE_RELATIVE,
        0.0},
    {GCOL_TRANSFER_BT1886, GCOL_REFERENCE_DISPLAY, GCOL_SAMPLE_SCALE_RELATIVE,
        0.0},
    {GCOL_TRANSFER_PQ, GCOL_REFERENCE_DISPLAY, GCOL_SAMPLE_SCALE_ABSOLUTE,
        10000.0},
    {GCOL_TRANSFER_HLG, GCOL_REFERENCE_SCENE, GCOL_SAMPLE_SCALE_RELATIVE, 0.0},
};

GCOL_API bool gcol_transfer_conventions(GCOL_Transfer transfer,
    GCOL_Reference * out_reference, GCOL_Sample_Scale * out_scale,
    double * out_white_luminance) {
  if (out_reference) {
    *out_reference = GCOL_REFERENCE_UNKNOWN;
  }
  if (out_scale) {
    *out_scale = GCOL_SAMPLE_SCALE_UNKNOWN;
  }
  if (out_white_luminance) {
    *out_white_luminance = 0.0;
  }
  for (size_t i = 0; i < GCOL_ARRAY_SIZE(gcol_transfer_conventions_table);
      i++) {
    const gcol_transfer_convention_t * c = &gcol_transfer_conventions_table[i];
    if (c->transfer != transfer) {
      continue;
    }
    if (out_reference) {
      *out_reference = c->reference;
    }
    if (out_scale) {
      *out_scale = c->scale;
    }
    if (out_white_luminance) {
      *out_white_luminance = c->white_luminance;
    }
    return true;
  }
  return false;
}

