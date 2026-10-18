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
 * @file math.h
 *
 * Colour maths: gamut to XYZ, chromatic adaptation, transfer encode/decode.
 *
 * All public intermediates use double. Matrices are row-major 3x3 arrays of
 * nine doubles: index `3 * row + col`.
 */

#ifndef GHOTI_IO_GCOL_MATH_H
#define GHOTI_IO_GCOL_MATH_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/color.h>
#include <ghoti.io/color/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chromatic adaptation method for ::gcol_chromatic_adapt.
 */
typedef enum {
  GCOL_CAT_NONE = 0,     ///< Identity; ignores @a from and @a to.
  GCOL_CAT_XYZ_SCALING,  ///< Von Kries in XYZ (diagonal scale).
  GCOL_CAT_BRADFORD,     ///< Bradford cone response (ICC's usual choice).
  GCOL_CAT_CAT02,        ///< CIECAM02 cone response.
  GCOL_CAT_COUNT
} GCOL_Adaptation;

/**
 * @brief Build the 3x3 that maps linear RGB in @p g to CIE XYZ.
 *
 * Requires three primaries and a white point (all with y > 0). A half-stated
 * gamut is refused rather than completed with an invented white.
 *
 * @param g The gamut. Must not be NULL.
 * @param m Out: row-major RGB→XYZ. Untouched on failure.
 * @return ::GCOL_OK, ::GCOL_ERR_INVALID, or ::GCOL_ERR_UNSUPPORTED if singular.
 */
GCOL_API GCOL_Result gcol_gamut_to_xyz(const GCOL_Gamut * g, double m[9]);

/**
 * @brief The inverse of ::gcol_gamut_to_xyz: XYZ→linear RGB in @p g.
 */
GCOL_API GCOL_Result gcol_xyz_to_gamut(const GCOL_Gamut * g, double m[9]);

/**
 * @brief Chromatic adaptation matrix taking XYZ under @p from to XYZ under @p to.
 *
 * @param from Source white point.
 * @param to Destination white point.
 * @param method Which adaptation.
 * @param m Out: row-major 3x3.
 */
GCOL_API GCOL_Result gcol_chromatic_adapt(GCOL_Chromaticity from,
    GCOL_Chromaticity to, GCOL_Adaptation method, double m[9]);

/**
 * @brief Decode an encoded sample to linear light using @p info's transfer.
 *
 * Unknown or incomplete transfer returns the input unchanged. PQ and HLG use
 * their published EOTF/OETF. For PQ the result is normalised to [0, 1] of
 * 10000 cd/m^2; use ::gcol_linear_to_nits to obtain absolute luminance.
 */
GCOL_API double gcol_transfer_decode(
    const GCOL_Color_Info * info, double encoded);

/**
 * @brief Encode linear light to a transfer-coded sample.
 *
 * For PQ, @p linear is normalised [0, 1] of 10000 cd/m^2.
 */
GCOL_API double gcol_transfer_encode(
    const GCOL_Color_Info * info, double linear);

/**
 * @brief Convert PQ-normalised (or absolute) linear light to cd/m^2.
 *
 * Uses @p info->white_luminance when set, else the transfer convention peak
 * (10000 for PQ). Relative transfers return @p linear unchanged.
 */
GCOL_API double gcol_linear_to_nits(
    const GCOL_Color_Info * info, double linear);

/**
 * @brief Inverse of ::gcol_linear_to_nits.
 */
GCOL_API double gcol_nits_to_linear(const GCOL_Color_Info * info, double nits);

/**
 * @brief BT.2390 PQ EETF: map @p nits_in from a mastering peak to a target.
 *
 * Soft-knees highlights so content mastered at @p source_peak fits a display
 * (or remit) of @p target_peak. @p target_black is the target black level in
 * cd/m^2 (0 for true black). Returns cd/m^2. Non-PQ call sites should not
 * use this; it is the curve behind ::GCOL_TRANSFORM_TONE_MAP.
 *
 * @param nits_in Source luminance in cd/m^2 (may exceed @p source_peak).
 * @param source_peak Mastering / content peak in cd/m^2; must be > 0.
 * @param target_peak Target peak in cd/m^2; must be > 0.
 * @param target_black Target black in cd/m^2; 0 when unstated.
 */
GCOL_API double gcol_tone_map_pq_eetf(double nits_in, double source_peak,
    double target_peak, double target_black);

/**
 * @brief Decode a 16-bit SGI LogL pixel (TIFF photometric 32844) to Y.
 *
 * Bit 15 is the sign; the other fifteen are log2 luminance in 1/256 steps
 * from 2^-64.  Code 0 (and 0x8000) is Y = 0.  Y is absolute and
 * scene-referred, in the file's units: multiply by the TIFF StoNits tag for
 * cd/m^2.
 */
GCOL_API double gcol_logl16_to_y(uint16_t encoded);

/**
 * @brief Encode Y as a 16-bit SGI LogL pixel; truncates, as libtiff does.
 *
 * Magnitudes past 2^64 saturate to 0x7fff (or 0xffff); below the smallest
 * code, and NaN, give 0.
 */
GCOL_API uint16_t gcol_y_to_logl16(double y);

/**
 * @brief Decode a 32-bit SGI LogLuv pixel (TIFF SGILOG) to CIE XYZ.
 *
 * The upper half is a LogL16 code (see ::gcol_logl16_to_y), the lower two
 * bytes u' and v' in 1/410 steps.  Y is absolute and scene-referred, in the
 * file's units; X and Z are on the same scale.  A negative Y (the sign bit)
 * decodes as black, as libtiff does; ::gcol_logl16_to_y keeps the sign.
 *
 * @return ::GCOL_OK, or ::GCOL_ERR_INVALID if @p xyz is NULL.
 */
GCOL_API GCOL_Result gcol_logluv32_to_xyz(uint32_t encoded, double xyz[3]);

/**
 * @brief Encode CIE XYZ as a 32-bit SGI LogLuv pixel.
 *
 * Truncates, as libtiff's undithered encoder does.  A chromaticity that is
 * undefined (Y = 0, or X + 15Y + 3Z not positive) is written as equal-energy
 * white.
 */
GCOL_API GCOL_Result gcol_xyz_to_logluv32(const double xyz[3], uint32_t * out);

/**
 * @brief Decode a 24-bit SGI LogLuv pixel (TIFF SGILOG24) to CIE XYZ.
 *
 * Bits 14..23 are log2 luminance in 1/64 steps from 2^-12 (so Y < 16);
 * bits 0..13 index a cell of a 0.0035 grid over the u'v' gamut.  An index
 * past the last cell decodes as equal-energy white, as libtiff does.  Only
 * the low 24 bits of @p encoded are read.
 *
 * @return ::GCOL_OK, or ::GCOL_ERR_INVALID if @p xyz is NULL.
 */
GCOL_API GCOL_Result gcol_logluv24_to_xyz(uint32_t encoded, double xyz[3]);

/**
 * @brief Encode CIE XYZ as a 24-bit SGI LogLuv pixel.
 *
 * Truncates; Y of 16 and above saturates.  An undefined chromaticity is
 * written as equal-energy white's cell.  One outside the grid is written as
 * the first cell on the line from it to equal-energy white; libtiff picks a
 * boundary cell by angle instead, so for those pixels the two can differ by
 * a cell.
 */
GCOL_API GCOL_Result gcol_xyz_to_logluv24(const double xyz[3], uint32_t * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_MATH_H */
