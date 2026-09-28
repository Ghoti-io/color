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
 * Absolute luminance helpers and SGI LogLuv32 encode/decode.
 *
 * LogLuv (Greg Ward) is the absolute scene-referred encoding TIFF uses under
 * PhotometricInterpretation SGILOG. libs/image can decode the TIFF container;
 * these helpers turn the 32-bit samples into XYZ the colour engine can use.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include <math.h>
#include <stdint.h>

GCOL_API double gcol_linear_to_nits(
    const GCOL_Color_Info * info, double linear) {
  if (!info) {
    return linear;
  }
  if (info->sample_scale != GCOL_SAMPLE_SCALE_ABSOLUTE &&
      info->transfer != GCOL_TRANSFER_PQ) {
    return linear;
  }
  double peak = info->white_luminance;
  if (!(peak > 0.0)) {
    double conv = 0.0;
    (void)gcol_transfer_conventions(info->transfer, NULL, NULL, &conv);
    peak = conv > 0.0 ? conv : 10000.0;
  }
  if (info->transfer == GCOL_TRANSFER_PQ) {
    /* decode yields fraction of 10000 nits. */
    return linear * 10000.0;
  }
  return linear * peak;
}

GCOL_API double gcol_nits_to_linear(const GCOL_Color_Info * info, double nits) {
  if (!info) {
    return nits;
  }
  if (info->transfer == GCOL_TRANSFER_PQ) {
    return nits / 10000.0;
  }
  double peak = info->white_luminance;
  if (!(peak > 0.0)) {
    double conv = 0.0;
    (void)gcol_transfer_conventions(info->transfer, NULL, NULL, &conv);
    peak = conv > 0.0 ? conv : 1.0;
  }
  if (info->sample_scale == GCOL_SAMPLE_SCALE_ABSOLUTE) {
    return nits / peak;
  }
  return nits;
}

/*
 * SGI LogLuv32 (Ward):
 *   Le = 256*(log2(Y) + 64)   stored in top 16 bits (unsigned)
 *   ue, ve = 256 * u', v'     bottom two bytes
 * where u' = 4X/(X+15Y+3Z), v' = 9Y/(X+15Y+3Z).
 */

GCOL_API GCOL_Result gcol_logluv32_to_xyz(uint32_t encoded, double xyz[3]) {
  if (!xyz) {
    return GCOL_ERR_INVALID;
  }
  const unsigned Le = (encoded >> 16) & 0xffffu;
  const unsigned ue = (encoded >> 8) & 0xffu;
  const unsigned ve = encoded & 0xffu;
  const double Y =
      exp((((double)Le + 0.5) / 256.0 - 64.0) * 0.6931471805599453);
  const double up = ((double)ue + 0.5) / 256.0;
  const double vp = ((double)ve + 0.5) / 256.0;
  if (!(vp > 1e-12)) {
    xyz[0] = xyz[1] = xyz[2] = 0.0;
    return GCOL_OK;
  }
  /* X = 9Yu'/(4v'), Z = X*(1/u' - 1) - 15Y/3  ... standard Ward form: */
  const double X = Y * 9.0 * up / (4.0 * vp);
  const double Z = Y * (12.0 - 3.0 * up - 20.0 * vp) / (4.0 * vp);
  xyz[0] = X;
  xyz[1] = Y;
  xyz[2] = Z > 0.0 ? Z : 0.0;
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_xyz_to_logluv32(const double xyz[3], uint32_t * out) {
  if (!xyz || !out) {
    return GCOL_ERR_INVALID;
  }
  const double X = xyz[0], Y = xyz[1], Z = xyz[2];
  if (!(Y > 0.0)) {
    *out = 0;
    return GCOL_OK;
  }
  const double d = X + 15.0 * Y + 3.0 * Z;
  if (!(d > 0.0)) {
    *out = 0;
    return GCOL_OK;
  }
  const double up = 4.0 * X / d;
  const double vp = 9.0 * Y / d;
  double Le = 256.0 * (log(Y) / 0.6931471805599453 + 64.0);
  if (Le < 0.0) {
    Le = 0.0;
  }
  if (Le > 65535.0) {
    Le = 65535.0;
  }
  unsigned ue = (unsigned)(up * 256.0);
  unsigned ve = (unsigned)(vp * 256.0);
  if (ue > 255u) {
    ue = 255u;
  }
  if (ve > 255u) {
    ve = 255u;
  }
  *out = ((uint32_t)Le << 16) | (ue << 8) | ve;
  return GCOL_OK;
}
