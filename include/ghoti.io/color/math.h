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
 * @brief Decode a 32-bit SGI LogLuv pixel (TIFF SGILOG) to CIE XYZ.
 *
 * LogLuv is scene-referred and absolute: Y is in cd/m^2 (approximately).
 * The XYZ can be fed into a transform whose source is linear XYZ/RGB.
 */
GCOL_API GCOL_Result gcol_logluv32_to_xyz(uint32_t encoded, double xyz[3]);

/**
 * @brief Encode CIE XYZ as a 32-bit SGI LogLuv pixel.
 */
GCOL_API GCOL_Result gcol_xyz_to_logluv32(const double xyz[3], uint32_t * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_MATH_H */
