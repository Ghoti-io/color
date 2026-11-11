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
 * Chromatic adaptation matrices (von Kries in a chosen cone space).
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include "matrix_internal.h"

#include <math.h>

/* Bradford; same digits as ICC fixtures' generate.py. */
static const double gcol_bradford[9] = {
    0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};

/* CIECAM02 CAT02. */
static const double gcol_cat02[9] = {
    0.7328, 0.4296, -0.1624, -0.7036, 1.6975, 0.0061, 0.0030, 0.0136, 0.9834};

static const double * cone_matrix(GCOL_Adaptation method) {
  switch (method) {
  case GCOL_CAT_BRADFORD:
    return gcol_bradford;
  case GCOL_CAT_CAT02:
    return gcol_cat02;
  default:
    return NULL;
  }
}

GCOL_API GCOL_Result gcol_chromatic_adapt(GCOL_Chromaticity from,
    GCOL_Chromaticity to, GCOL_Adaptation method, double m[9]) {
  if (!m) {
    return GCOL_ERR_INVALID;
  }
  if (method == GCOL_CAT_NONE) {
    for (int i = 0; i < 9; i++) {
      m[i] = (i % 4 == 0) ? 1.0 : 0.0;
    }
    return GCOL_OK;
  }
  /* The arguments are widened here: a float given to isfinite is converted back by the Windows headers' macro, which -Wfloat-conversion refuses. */
  if (!(from.y > 0.0) || !(to.y > 0.0) || !isfinite((double) from.x) || !isfinite((double) from.y) ||
      !isfinite((double) to.x) || !isfinite((double) to.y)) {
    return GCOL_ERR_INVALID;
  }

  double src[3], dst[3];
  gcol_xy_to_xyz(from.x, from.y, src);
  gcol_xy_to_xyz(to.x, to.y, dst);

  if (method == GCOL_CAT_XYZ_SCALING) {
    if (!(fabs(src[0]) > 1e-18) || !(fabs(src[1]) > 1e-18) ||
        !(fabs(src[2]) > 1e-18)) {
      return GCOL_ERR_UNSUPPORTED;
    }
    for (int i = 0; i < 9; i++) {
      m[i] = 0.0;
    }
    m[0] = dst[0] / src[0];
    m[4] = dst[1] / src[1];
    m[8] = dst[2] / src[2];
    return GCOL_OK;
  }

  const double * ma = cone_matrix(method);
  if (!ma) {
    return GCOL_ERR_INVALID;
  }
  double inv_ma[9];
  if (!gcol_mat3_inv(ma, inv_ma)) {
    return GCOL_ERR_INTERNAL;
  }
  double cone_src[3], cone_dst[3];
  gcol_mat3_vec(ma, src, cone_src);
  gcol_mat3_vec(ma, dst, cone_dst);
  if (!(fabs(cone_src[0]) > 1e-18) || !(fabs(cone_src[1]) > 1e-18) ||
      !(fabs(cone_src[2]) > 1e-18)) {
    return GCOL_ERR_UNSUPPORTED;
  }
  double diag[9] = {0};
  diag[0] = cone_dst[0] / cone_src[0];
  diag[4] = cone_dst[1] / cone_src[1];
  diag[8] = cone_dst[2] / cone_src[2];
  double tmp[9];
  gcol_mat3_mul(diag, ma, tmp);
  gcol_mat3_mul(inv_ma, tmp, m);
  return GCOL_OK;
}
