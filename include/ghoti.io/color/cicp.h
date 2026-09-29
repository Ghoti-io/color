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
 * @file cicp.h
 *
 * ITU-T H.273 coding-independent code points → ::GCOL_Color_Info.
 *
 * Containers (PNG `cICP`, AVIF, HEIF, MP4) carry these integers. Parsing the
 * container stays in that codec; knowing what the integers *mean* is colour.
 */

#ifndef GHOTI_IO_GCOL_CICP_H
#define GHOTI_IO_GCOL_CICP_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/color.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill @p out from H.273 ColourPrimaries and TransferCharacteristics.
 *
 * Maps Table 2 (colour primaries) and Table 3 (transfer characteristics)
 * into a ::GCOL_Color_Info. Unrecognised codes leave the corresponding fields
 * unset. Matrix coefficients and video full-range flag are **not** interpreted
 * here: a container that constrains them (PNG Third Edition requires matrix 0
 * and full range for RGB) validates those itself before calling.
 *
 * Does not call ::gcol_color_info_default; the caller owns that. Recognised
 * transfers also apply ::gcol_transfer_conventions (so PQ states absolute
 * 10000 cd/m² once).
 *
 * Code point 11 (theatrical DCI-P3) is deliberately unmapped: it shares
 * Display P3's primaries under a different white, which would break the
 * "no two named gamuts share their primaries" invariant
 * ::gcol_gamut_identify relies on.
 *
 * @param colour_primaries H.273 ColourPrimaries code point.
 * @param transfer_characteristics H.273 TransferCharacteristics code point.
 * @param out Description to fill. Must not be NULL.
 * @return true when at least one of the two codes was recognised and written.
 */
GCOL_API bool gcol_cicp_to_color_info(unsigned colour_primaries,
    unsigned transfer_characteristics, GCOL_Color_Info * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_CICP_H */
