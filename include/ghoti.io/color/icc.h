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
 * @file icc.h
 *
 * ICC profile parse, synthesise, and bridge to ::GCOL_Color_Info.
 *
 * Matrix/TRC RGB and grayscale profiles are recovered into a description.
 * LUT profiles (mft1, mft2, mAB , mBA ) parse and transform via
 * ::gcol_transform_create_icc; ::gcol_icc_to_color_info still returns
 * ::GCOL_ERR_UNSUPPORTED for them because a LUT is not a gamut+transfer pair.
 *
 * Perceptual and saturation intents have no independent specification. When a
 * profile carries A2B0/B2A0 or A2B2/B2A2, those tables are applied under the
 * matching intent; when they are absent, that intent returns
 * ::GCOL_ERR_UNSUPPORTED rather than inventing a vendor mapping. Absolute
 * colorimetric on the LUT path uses the colorimetric tables and scales by
 * media white / D50. Colorimetric intents are specification-defined.
 */

#ifndef GHOTI_IO_GCOL_ICC_H
#define GHOTI_IO_GCOL_ICC_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/color.h>
#include <ghoti.io/color/core.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque parsed ICC profile.
 */
typedef struct GCOL_ICC_Profile GCOL_ICC_Profile;

/**
 * @brief Parse an ICC profile from @p bytes.
 *
 * Enforces ::GCOL_Limits (tag count, curve samples, profile size). Matrix/TRC
 * RGB and grayscale profiles are accepted; others return
 * ::GCOL_ERR_UNSUPPORTED after a successful header/tag-table parse only when
 * the class or colour space is known but not yet handled - a truncated or
 * contradictory tag table is ::GCOL_ERR_CORRUPT.
 *
 * @param alloc Allocator for the returned object, or NULL for the default.
 * @param limits Caps, or NULL for defaults.
 * @param bytes Profile bytes. Copied; the caller may free them afterwards.
 * @param size Byte length of @p bytes.
 * @param out Receives the profile. Free with ::gcol_icc_free.
 */
GCOL_API GCOL_Result gcol_icc_parse(const GCOL_Allocator * alloc,
    const GCOL_Limits * limits, const void * bytes, size_t size,
    GCOL_ICC_Profile ** out);

/**
 * @brief Free a profile from ::gcol_icc_parse.
 */
GCOL_API void gcol_icc_free(GCOL_ICC_Profile * profile);

/**
 * @brief Profile version packed as in the header (e.g. 0x02100000 for 2.1).
 */
GCOL_API uint32_t gcol_icc_version(const GCOL_ICC_Profile * profile);

/**
 * @brief Profile class four-character code (e.g. 'mntr').
 */
GCOL_API uint32_t gcol_icc_class(const GCOL_ICC_Profile * profile);

/**
 * @brief Colour space of data ('RGB ', 'GRAY', …).
 */
GCOL_API uint32_t gcol_icc_data_space(const GCOL_ICC_Profile * profile);

/**
 * @brief Profile connection space ('XYZ ' or 'Lab ').
 */
GCOL_API uint32_t gcol_icc_pcs(const GCOL_ICC_Profile * profile);

/**
 * @brief Header rendering intent.
 */
GCOL_API GCOL_Rendering_Intent gcol_icc_intent(
    const GCOL_ICC_Profile * profile);

/**
 * @brief ASCII description from the desc tag, or NULL if absent.
 */
GCOL_API const char * gcol_icc_description(const GCOL_ICC_Profile * profile);

/**
 * @brief True when the profile is matrix/TRC (not LUT-based).
 */
GCOL_API bool gcol_icc_is_matrix_trc(const GCOL_ICC_Profile * profile);

/**
 * @brief ICC profile ID (MD5 with flags, intent, and ID fields zeroed).
 *
 * Writes 16 bytes. All-zero means the profile did not carry a computed ID
 * and this library left the field unpopulated in a write; a parse always
 * recomputes it.
 */
GCOL_API GCOL_Result gcol_icc_profile_id(
    const GCOL_ICC_Profile * profile, unsigned char out[16]);

/**
 * @brief Recover a colour description from a matrix/TRC profile.
 *
 * RGB matrix/TRC profiles fill gamut (primaries and white) and transfer.
 * Grayscale matrix/TRC fills transfer only. LUT profiles and other classes
 * return ::GCOL_ERR_UNSUPPORTED.
 */
GCOL_API GCOL_Result gcol_icc_to_color_info(
    const GCOL_ICC_Profile * profile, GCOL_Color_Info * out);

/**
 * @brief Write an ICC profile that says what @p info says.
 *
 * Manufactured, not repeated: use only when the caller stated a colour model
 * and the file has nowhere else to put it, never in place of a carried
 * profile. Both primaries and transfer must be known.
 *
 * @param alloc Allocator for the returned buffer, or NULL for the default.
 * @param info The colour model to state.
 * @param out_bytes Receives the profile; free with gcol_icc_bytes_free().
 *   NULL when @p info does not state enough.
 * @param out_size Receives the profile size, or 0.
 * @return GCOL_OK (including when nothing was built), or GCOL_ERR_OOM /
 *   GCOL_ERR_INVALID.
 */
GCOL_API GCOL_Result gcol_icc_write(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * info, void ** out_bytes, size_t * out_size);

/**
 * @brief Free a buffer returned by gcol_icc_write().
 */
GCOL_API void gcol_icc_bytes_free(const GCOL_Allocator * alloc, void * bytes);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_ICC_H */
