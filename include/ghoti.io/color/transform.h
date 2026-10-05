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
 * @file transform.h
 *
 * Pixel transforms between two colour descriptions.
 *
 * A ::GCOL_Transform is immutable after ::gcol_transform_create returns and
 * may be shared across threads. Creation itself is not synchronised. A
 * caller-supplied allocator must be safe for concurrent use if creation races.
 *
 * Narrowing from float to an integer sample truncates; widening from an
 * integer sample to float does not need a rounding rule. Values outside the
 * unit interval are clipped before encoding unless absolute HDR keeps them.
 *
 * Perceptual and saturation intents have no specification of their own.
 * When an ICC profile carries an A2B0/B2A0 (perceptual) or A2B2/B2A2
 * (saturation) table, requesting that intent applies the table — those
 * bytes are the profile author's mapping. When no such table exists, the
 * intent returns ::GCOL_ERR_UNSUPPORTED rather than inventing a gamut map.
 * Colorimetric intents are specification-defined; absolute colorimetric on
 * the LUT path scales relative PCS XYZ by media_white / D50.
 */

#ifndef GHOTI_IO_GCOL_TRANSFORM_H
#define GHOTI_IO_GCOL_TRANSFORM_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/color.h>
#include <ghoti.io/color/core.h>
#include <ghoti.io/color/icc.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Storage type of each colour sample in a buffer.
 */
typedef enum {
  GCOL_SAMPLE_UINT8 = 0, ///< Interleaved 0..255.
  GCOL_SAMPLE_FLOAT32,   ///< Interleaved unit-interval float.
  GCOL_SAMPLE_FLOAT64,   ///< Interleaved unit-interval double.
  GCOL_SAMPLE_TYPE_COUNT
} GCOL_Sample_Type;

/**
 * @brief Colour model of interleaved samples in a buffer.
 */
typedef enum {
  GCOL_PIXEL_RGB = 0, ///< 3 = RGB, 4 = RGBA (alpha copied through).
  GCOL_PIXEL_CMYK,    ///< 4 channels; polarity from the transform source.
  GCOL_PIXEL_COUNT
} GCOL_Pixel_Model;

/**
 * @brief How samples sit in memory for ::gcol_transform_apply.
 *
 * Interleaved only. Nothing here describes compression, subsampling, or
 * palettes - that stays in libs/image.
 */
typedef struct GCOL_Sample_Layout {
  GCOL_Sample_Type type;  ///< Storage type of one sample.
  unsigned channel_count; ///< RGB: 3 or 4; CMYK: 4.
  GCOL_Pixel_Model model; ///< ::GCOL_PIXEL_RGB by default (zero-init).
} GCOL_Sample_Layout;

/**
 * @brief Opaque transform between two colour spaces.
 */
typedef struct GCOL_Transform GCOL_Transform;

/** Clip out-of-gamut linear RGB to [0, 1] before encoding (default on). */
#define GCOL_TRANSFORM_CLIP 1u

/**
 * Black-point compensation. Opt-in: there is no defensible default.
 * Matrix path uses @c black_luminance / @c white_luminance; LUT path uses
 * ICC `bkpt` when present (else no-op).
 */
#define GCOL_TRANSFORM_BPC 2u

/**
 * BT.2390 PQ EETF when mapping absolute PQ into a lower peak (or relative
 * destination). Opt-in; without it absolute PQ still uses a hard luminance
 * scale through @c white_luminance.
 */
#define GCOL_TRANSFORM_TONE_MAP 4u

/**
 * @brief Build a transform from @p src into @p dst.
 *
 * Supports matrix/TRC RGB descriptions under relative and absolute
 * colorimetric intents (PCS XYZ). Relative adapts through D50 (Bradford);
 * absolute skips chromatic adaptation so illuminant differences remain.
 * Absolute PQ sources are mapped into a relative destination by scaling
 * through white_luminance unless ::GCOL_TRANSFORM_TONE_MAP is set, which
 * applies the BT.2390 PQ EETF. Other intents return ::GCOL_ERR_UNSUPPORTED.
 *
 * @param alloc Allocator, or NULL for the default.
 * @param src Source description. Must state primaries, white, and transfer.
 * @param dst Destination description; same requirements.
 * @param intent Rendering intent.
 * @param flags Bitwise-or of GCOL_TRANSFORM_* flags; 0 means CLIP.
 * @param out Receives the transform. Free with ::gcol_transform_free.
 */
GCOL_API GCOL_Result gcol_transform_create(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, const GCOL_Color_Info * dst,
    GCOL_Rendering_Intent intent, uint32_t flags, GCOL_Transform ** out);

/**
 * @brief Build a transform from a parsed ICC profile into @p dst.
 *
 * Used for LUT and CMYK profiles. @p cmyk_polarity selects how CMYK bytes are
 * read: ::GCOL_CMYK_POLARITY_INK inverts (Adobe/JPEG), ::GCOL_CMYK_POLARITY_REFLECTION
 * passes device values through (0 = no ink). Unknown polarity is refused for
 * CMYK sources.
 *
 * Relative colorimetric prefers A2B1 (falling back to A2B0 when that is the
 * only AToB). Absolute uses the same tables, then scales PCS XYZ by
 * media_white / D50. Perceptual uses A2B0 only; saturation uses A2B2 only;
 * without the matching table either is refused.
 */
GCOL_API GCOL_Result gcol_transform_create_icc(const GCOL_Allocator * alloc,
    const GCOL_ICC_Profile * src, const GCOL_Color_Info * dst,
    GCOL_Rendering_Intent intent, uint32_t flags,
    GCOL_CMYK_Polarity cmyk_polarity, GCOL_Transform ** out);

/**
 * @brief Build a transform from @p src into a parsed ICC profile (BToA).
 *
 * Destination CMYK/LUT path. Intent selection matches
 * ::gcol_transform_create_icc (B2A1 for relative/absolute, B2A0 for
 * perceptual, B2A2 for saturation). Lab PCS B2A is supported.
 */
GCOL_API GCOL_Result gcol_transform_create_to_icc(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, const GCOL_ICC_Profile * dst,
    GCOL_Rendering_Intent intent, uint32_t flags,
    GCOL_CMYK_Polarity cmyk_polarity, GCOL_Transform ** out);

/**
 * @brief Free a transform from ::gcol_transform_create.
 */
GCOL_API void gcol_transform_free(GCOL_Transform * xform);

/**
 * @brief Convert @p pixel_count pixels from @p src into @p dst.
 *
 * Buffers are interleaved per ::GCOL_Sample_Layout. Source and destination
 * layouts may differ in type and in whether alpha is present.
 */
GCOL_API GCOL_Result gcol_transform_apply(const GCOL_Transform * xform,
    const GCOL_Sample_Layout * src_layout, const void * src,
    const GCOL_Sample_Layout * dst_layout, void * dst, size_t pixel_count);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_TRANSFORM_H */
