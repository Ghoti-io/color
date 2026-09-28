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

#ifndef GHOTI_IO_GCOL_MATRIX_INTERNAL_H
#define GHOTI_IO_GCOL_MATRIX_INTERNAL_H

#include <ghoti.io/color/macros.h>

void gcol_mat3_mul(const double a[9], const double b[9], double out[9]);
void gcol_mat3_vec(const double m[9], const double v[3], double out[3]);
int gcol_mat3_inv(const double m[9], double out[9]);
void gcol_xy_to_xyz(double x, double y, double out[3]);

#endif /* GHOTI_IO_GCOL_MATRIX_INTERNAL_H */
