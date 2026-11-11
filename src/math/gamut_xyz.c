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
 * Build RGB↔XYZ matrices from a stated gamut.
 *
 * Same construction as libs/image's ICC fixture generator: columns are the
 * primary XYZs scaled so that RGB=(1,1,1) lands on the white point.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include "matrix_internal.h"

#include <math.h>

static int chroma_usable(const GCOL_Chromaticity * c) {
  return c->y > 0.0 && isfinite((double) c->x) && isfinite((double) c->y);
}

GCOL_API GCOL_Result gcol_gamut_to_xyz(const GCOL_Gamut * g, double m[9]) {
  if (!g || !m) {
    return GCOL_ERR_INVALID;
  }
  if (!chroma_usable(&g->white) || !chroma_usable(&g->red) ||
      !chroma_usable(&g->green) || !chroma_usable(&g->blue)) {
    return GCOL_ERR_INVALID;
  }

  double r[3], gr[3], b[3], w[3];
  gcol_xy_to_xyz(g->red.x, g->red.y, r);
  gcol_xy_to_xyz(g->green.x, g->green.y, gr);
  gcol_xy_to_xyz(g->blue.x, g->blue.y, b);
  gcol_xy_to_xyz(g->white.x, g->white.y, w);

  /* Columns are the unscaled primaries. */
  double prim[9] = {r[0], gr[0], b[0], r[1], gr[1], b[1], r[2], gr[2], b[2]};
  double inv[9];
  if (!gcol_mat3_inv(prim, inv)) {
    return GCOL_ERR_UNSUPPORTED;
  }
  double scale[3];
  gcol_mat3_vec(inv, w, scale);
  for (int col = 0; col < 3; col++) {
    m[0 + col] = prim[0 + col] * scale[col];
    m[3 + col] = prim[3 + col] * scale[col];
    m[6 + col] = prim[6 + col] * scale[col];
  }
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_xyz_to_gamut(const GCOL_Gamut * g, double m[9]) {
  double forward[9];
  GCOL_Result r = gcol_gamut_to_xyz(g, forward);
  if (r != GCOL_OK) {
    return r;
  }
  if (!gcol_mat3_inv(forward, m)) {
    return GCOL_ERR_UNSUPPORTED;
  }
  return GCOL_OK;
}
