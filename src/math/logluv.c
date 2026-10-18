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
 * Absolute luminance helpers and SGI LogLuv encode/decode.
 *
 * LogLuv (Greg Ward) is the absolute scene-referred encoding TIFF uses under
 * PhotometricInterpretation SGILOG. libs/image can decode the TIFF container;
 * these helpers turn its samples into XYZ the color engine can use.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include <math.h>
#include <stdbool.h>
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
 * SGI LogLuv (Larson, "LogLuv encoding for full-gamut, high-dynamic range
 * images", 1998), the three forms TIFF carries:
 *
 *   LogL16    bit 15 the sign of Y, bits 0..14 Le: Y = 2^((Le + 1/2)/256 - 64)
 *   LogLuv32  LogL16 in the upper half; then ue, ve = floor(410 u'),
 *             floor(410 v')
 *   LogLuv24  bits 14..23 Le: Y = 2^((Le + 1/2)/64 - 12); bits 0..13 index a
 *             cell of a square grid over the u'v' gamut (logluv_uv_table.h)
 *
 * Le = 0 means Y = 0 in each, and LogLuv32 and LogLuv24 decode a Y that is
 * not positive as black.  The encoders truncate rather than round, and write
 * an undefined chromaticity (Y encoded as 0, or X + 15Y + 3Z not positive) as
 * equal-energy white, u' = 4/19, v' = 9/19.  All of that is what libtiff
 * does, and tests/data/logluv/ holds its answers.
 *
 * One place this differs from libtiff on purpose: a chromaticity outside the
 * 24-bit grid.  It is written here as the first cell met on the line from it
 * to equal-energy white.  libtiff instead buckets the direction from white
 * into angles and takes a boundary cell per bucket, which is defined by its
 * code and not by any description of the format, so it is not reproduced;
 * such a pixel can encode to a neighbouring boundary cell.
 */

#include "logluv_uv_table.h"

#define LOGLUV_UV_SCALE 410.0
#define LOGLUV_U_NEUTRAL (4.0 / 19.0)
#define LOGLUV_V_NEUTRAL (9.0 / 19.0)

static void uv_to_xyz(double Y, double up, double vp, double xyz[3]) {
  if (!(Y > 0.0)) {
    xyz[0] = xyz[1] = xyz[2] = 0.0;
    return;
  }
  // CIE 1976 u'v' to xy, then xyY to XYZ.
  const double d = 6.0 * up - 16.0 * vp + 12.0;
  const double x = 9.0 * up / d;
  const double y = 4.0 * vp / d;
  xyz[0] = x / y * Y;
  xyz[1] = Y;
  xyz[2] = (1.0 - x - y) / y * Y;
}

// u'v' of XYZ, or false where the chromaticity is undefined.
static bool xyz_to_uv(const double xyz[3], double * up, double * vp) {
  const double s = xyz[0] + 15.0 * xyz[1] + 3.0 * xyz[2];
  if (!(s > 0.0)) {
    return false;
  }
  *up = 4.0 * xyz[0] / s;
  *vp = 9.0 * xyz[1] / s;
  return true;
}

GCOL_API double gcol_logl16_to_y(uint16_t encoded) {
  const unsigned le = encoded & 0x7fffu;
  if (le == 0u) {
    return 0.0;
  }
  const double y = exp2(((double)le + 0.5) / 256.0 - 64.0);
  return (encoded & 0x8000u) ? -y : y;
}

GCOL_API uint16_t gcol_y_to_logl16(double y) {
  const double a = fabs(y);
  if (!(a > 0.0)) {
    return 0u; // Zero and NaN.
  }
  const double le = 256.0 * (log2(a) + 64.0);
  unsigned code;
  if (!(le < 32767.0)) {
    code = 0x7fffu;
  }
  else if (!(le >= 1.0)) {
    return 0u; // Below the smallest code; there is no negative zero.
  }
  else {
    code = (unsigned)le;
  }
  return (uint16_t)(y < 0.0 ? (0x8000u | code) : code);
}

GCOL_API GCOL_Result gcol_logluv32_to_xyz(uint32_t encoded, double xyz[3]) {
  if (!xyz) {
    return GCOL_ERR_INVALID;
  }
  const double Y = gcol_logl16_to_y((uint16_t)(encoded >> 16));
  const double up = ((double)((encoded >> 8) & 0xffu) + 0.5) / LOGLUV_UV_SCALE;
  const double vp = ((double)(encoded & 0xffu) + 0.5) / LOGLUV_UV_SCALE;
  uv_to_xyz(Y, up, vp, xyz);
  return GCOL_OK;
}

static unsigned uv_byte(double c) {
  if (!(c > 0.0)) {
    return 0u;
  }
  const double e = LOGLUV_UV_SCALE * c;
  return e >= 255.0 ? 255u : (unsigned)e;
}

GCOL_API GCOL_Result gcol_xyz_to_logluv32(const double xyz[3], uint32_t * out) {
  if (!xyz || !out) {
    return GCOL_ERR_INVALID;
  }
  const uint16_t le = gcol_y_to_logl16(xyz[1]);
  double up, vp;
  if ((le & 0x7fffu) == 0u || !xyz_to_uv(xyz, &up, &vp)) {
    up = LOGLUV_U_NEUTRAL;
    vp = LOGLUV_V_NEUTRAL;
  }
  *out = ((uint32_t)le << 16) | (uv_byte(up) << 8) | uv_byte(vp);
  return GCOL_OK;
}

// The cell holding u'v', or -1 outside the grid.
static int uv24_index(double up, double vp) {
  const double vrow = (vp - GCOL_LOGLUV24_UV_VSTART) / GCOL_LOGLUV24_UV_CELL;
  if (!(vrow >= 0.0) || vrow >= (double)GCOL_LOGLUV24_UV_ROWS) {
    return -1;
  }
  const gcol_logluv24_uv_row * row = &gcol_logluv24_uv_rows[(unsigned)vrow];
  const double ucol = (up - row->ustart) / GCOL_LOGLUV24_UV_CELL;
  if (!(ucol >= 0.0) || ucol >= (double)row->count) {
    return -1;
  }
  return (int)row->first + (int)(unsigned)ucol;
}

GCOL_API GCOL_Result gcol_logluv24_to_xyz(uint32_t encoded, double xyz[3]) {
  if (!xyz) {
    return GCOL_ERR_INVALID;
  }
  const unsigned le = (encoded >> 14) & 0x3ffu;
  const unsigned index = encoded & 0x3fffu;
  const double Y = le == 0u ? 0.0 : exp2(((double)le + 0.5) / 64.0 - 12.0);
  double up = LOGLUV_U_NEUTRAL, vp = LOGLUV_V_NEUTRAL;
  if (index < GCOL_LOGLUV24_UV_CELLS) {
    // The last row whose first cell is at or before the index.
    unsigned lo = 0u, hi = GCOL_LOGLUV24_UV_ROWS - 1u;
    while (lo < hi) {
      const unsigned mid = (lo + hi + 1u) / 2u;
      if (gcol_logluv24_uv_rows[mid].first <= index) {
        lo = mid;
      }
      else {
        hi = mid - 1u;
      }
    }
    const gcol_logluv24_uv_row * row = &gcol_logluv24_uv_rows[lo];
    up = row->ustart +
        ((double)(index - row->first) + 0.5) * GCOL_LOGLUV24_UV_CELL;
    vp = GCOL_LOGLUV24_UV_VSTART + ((double)lo + 0.5) * GCOL_LOGLUV24_UV_CELL;
  }
  uv_to_xyz(Y, up, vp, xyz);
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_xyz_to_logluv24(const double xyz[3], uint32_t * out) {
  if (!xyz || !out) {
    return GCOL_ERR_INVALID;
  }
  unsigned le = 0u;
  const double Y = xyz[1];
  if (Y > 0.0) {
    const double e = 64.0 * (log2(Y) + 12.0);
    le = !(e < 1023.0) ? 1023u : (e >= 1.0 ? (unsigned)e : 0u);
  }
  double up, vp;
  if (le == 0u || !xyz_to_uv(xyz, &up, &vp)) {
    up = LOGLUV_U_NEUTRAL;
    vp = LOGLUV_V_NEUTRAL;
  }
  int index = uv24_index(up, vp);
  if (index < 0) {
    // Outside the grid: the first cell on the way to white, which is inside
    // it.  Bisection keeps t_out outside and t_in inside.
    double t_out = 0.0, t_in = 1.0;
    for (int i = 0; i < 48; i++) {
      const double t = 0.5 * (t_out + t_in);
      if (uv24_index(up + t * (LOGLUV_U_NEUTRAL - up),
              vp + t * (LOGLUV_V_NEUTRAL - vp)) < 0) {
        t_out = t;
      }
      else {
        t_in = t;
      }
    }
    index = uv24_index(up + t_in * (LOGLUV_U_NEUTRAL - up),
        vp + t_in * (LOGLUV_V_NEUTRAL - vp));
  }
  *out = ((uint32_t)le << 14) | (uint32_t)index;
  return GCOL_OK;
}
