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

#ifndef GHOTI_IO_GCOL_ICC_LUT_H
#define GHOTI_IO_GCOL_ICC_LUT_H

#include <ghoti.io/color/macros.h>

#include "icc_internal.h"

/** Multi-dimensional LUT recovered from mft1 / mft2 / mAB / mBA. */
struct gcol_icc_lut_s {
  uint8_t input_chan;  ///< 1..4.
  uint8_t output_chan; ///< 1..4 (PCS XYZ/Lab, or device for B2A).
  uint8_t grid;        ///< CLUT points per axis; 0 when no CLUT.
  uint8_t clut_precision; ///< 1 or 2: bytes per CLUT sample (mft/mAB).
  bool has_matrix;     ///< Apply e[] after M curves (mft) or mid matrix (mAB).
  bool pcs_lab;        ///< Output is ICC Lab encoding rather than XYZ.
  bool pcs_to_device;  ///< True for B2A (PCS in → device out).
  double e[9];         ///< Row-major 3x3; identity when unused.
  gcol_icc_curve_t * in_curves;  ///< length input_chan; may be NULL.
  gcol_icc_curve_t * m_curves;   ///< length output_chan; may be NULL (mAB).
  gcol_icc_curve_t * out_curves; ///< length output_chan; may be NULL.
  /** CLUT samples as unit-interval doubles, length grid^input * output. */
  double * clut;
  size_t clut_count;
};

void gcol_icc_lut_clear(gcol_icc_lut_t * lut, const GCOL_Allocator * alloc);

/**
 * Parse one mft1/mft2/mAB /mBA  tag into @p out.
 * Enforces @p lim->lut_grid on the CLUT grid size.
 */
GCOL_Result gcol_icc_lut_parse(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * tag, size_t tag_size,
    bool pcs_lab, gcol_icc_lut_t * out);

/**
 * Apply device values in [0,1] through the LUT to PCS (XYZ or Lab→XYZ).
 * @p device length is lut->input_chan; @p xyz receives three XYZ values.
 */
GCOL_Result gcol_icc_lut_apply(const gcol_icc_lut_t * lut,
    const double * device, double xyz[3]);

/**
 * Apply PCS XYZ through a BToA LUT to device values in [0,1].
 * @p device length is lut->output_chan.
 */
GCOL_Result gcol_icc_lut_apply_b2a(const gcol_icc_lut_t * lut,
    const double xyz[3], double * device);

#endif /* GHOTI_IO_GCOL_ICC_LUT_H */
