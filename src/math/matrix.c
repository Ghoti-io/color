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
 * Internal 3x3 helpers shared by gamut and adaptation maths.
 */

#include "matrix_internal.h"

#include <math.h>

void gcol_mat3_mul(const double a[9], const double b[9], double out[9]) {
  double tmp[9];
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      tmp[3 * r + c] = a[3 * r + 0] * b[3 * 0 + c] + a[3 * r + 1] * b[3 * 1 + c] +
          a[3 * r + 2] * b[3 * 2 + c];
    }
  }
  for (int i = 0; i < 9; i++) {
    out[i] = tmp[i];
  }
}

void gcol_mat3_vec(const double m[9], const double v[3], double out[3]) {
  out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
  out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
  out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

int gcol_mat3_inv(const double m[9], double out[9]) {
  const double a = m[0], b = m[1], c = m[2];
  const double d = m[3], e = m[4], f = m[5];
  const double g = m[6], h = m[7], i = m[8];
  const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!(fabs(det) > 1e-18)) {
    return 0;
  }
  const double inv = 1.0 / det;
  out[0] = (e * i - f * h) * inv;
  out[1] = (c * h - b * i) * inv;
  out[2] = (b * f - c * e) * inv;
  out[3] = (f * g - d * i) * inv;
  out[4] = (a * i - c * g) * inv;
  out[5] = (c * d - a * f) * inv;
  out[6] = (d * h - e * g) * inv;
  out[7] = (b * g - a * h) * inv;
  out[8] = (a * e - b * d) * inv;
  return 1;
}

void gcol_xy_to_xyz(double x, double y, double out[3]) {
  out[0] = x / y;
  out[1] = 1.0;
  out[2] = (1.0 - x - y) / y;
}
