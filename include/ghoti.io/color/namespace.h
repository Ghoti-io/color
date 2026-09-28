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
 * @file namespace.h
 *
 * Maps every public name of this library into its version namespace.
 *
 * Kept in one file rather than beside each declaration: a type rename has to
 * be in effect before any struct tag that uses the name, and an internal
 * header may define such a tag without including the public header that
 * declares the typedef.
 *
 * `make check-symbols` fails if an exported symbol is missing from this list.
 *
 * See CONVENTIONS.md section 4.
 */

#ifndef GHOTI_IO_GCOL_NAMESPACE_H
#define GHOTI_IO_GCOL_NAMESPACE_H

#include <ghoti.io/color/libver.h>

/// @cond HIDDEN_SYMBOLS

/* Public types. GCU_* names are cutil's; cutil has already renamed them. */
#define GCOL_Allocator GHOTIIO_COLOR(GCOL_Allocator)
#define GCOL_Result GHOTIIO_COLOR(GCOL_Result)

#define gcol_allocator_default GHOTIIO_COLOR(gcol_allocator_default)
#define gcol_result_string GHOTIIO_COLOR(gcol_result_string)
#define gcol_version_number GHOTIIO_COLOR(gcol_version_number)
#define gcol_version_string GHOTIIO_COLOR(gcol_version_string)


/* Description types and functions (phase 0). */
#define GCOL_Chromaticity GHOTIIO_COLOR(GCOL_Chromaticity)
#define GCOL_Gamut GHOTIIO_COLOR(GCOL_Gamut)
#define GCOL_Primaries GHOTIIO_COLOR(GCOL_Primaries)
#define GCOL_Transfer GHOTIIO_COLOR(GCOL_Transfer)
#define GCOL_Reference GHOTIIO_COLOR(GCOL_Reference)
#define GCOL_Sample_Scale GHOTIIO_COLOR(GCOL_Sample_Scale)
#define GCOL_Rendering_Intent GHOTIIO_COLOR(GCOL_Rendering_Intent)
#define GCOL_CMYK_Polarity GHOTIIO_COLOR(GCOL_CMYK_Polarity)
#define GCOL_Color_Info GHOTIIO_COLOR(GCOL_Color_Info)

#define gcol_gamut_identify GHOTIIO_COLOR(gcol_gamut_identify)
#define gcol_gamut_named GHOTIIO_COLOR(gcol_gamut_named)
#define gcol_color_info_set_gamut GHOTIIO_COLOR(gcol_color_info_set_gamut)
#define gcol_transfer_conventions GHOTIIO_COLOR(gcol_transfer_conventions)
#define gcol_color_info_default GHOTIIO_COLOR(gcol_color_info_default)
#define gcol_icc_write GHOTIIO_COLOR(gcol_icc_write)
#define gcol_icc_bytes_free GHOTIIO_COLOR(gcol_icc_bytes_free)

/* Colour maths (phase 1). */
#define GCOL_Adaptation GHOTIIO_COLOR(GCOL_Adaptation)
#define gcol_gamut_to_xyz GHOTIIO_COLOR(gcol_gamut_to_xyz)
#define gcol_xyz_to_gamut GHOTIIO_COLOR(gcol_xyz_to_gamut)
#define gcol_chromatic_adapt GHOTIIO_COLOR(gcol_chromatic_adapt)
#define gcol_transfer_decode GHOTIIO_COLOR(gcol_transfer_decode)
#define gcol_transfer_encode GHOTIIO_COLOR(gcol_transfer_encode)
#define gcol_linear_to_nits GHOTIIO_COLOR(gcol_linear_to_nits)
#define gcol_nits_to_linear GHOTIIO_COLOR(gcol_nits_to_linear)
#define gcol_logluv32_to_xyz GHOTIIO_COLOR(gcol_logluv32_to_xyz)
#define gcol_xyz_to_logluv32 GHOTIIO_COLOR(gcol_xyz_to_logluv32)

/* ICC parse and transforms (phase 2–3). */
#define GCOL_Limits GHOTIIO_COLOR(GCOL_Limits)
#define GCOL_ICC_Profile GHOTIIO_COLOR(GCOL_ICC_Profile)
#define GCOL_Sample_Type GHOTIIO_COLOR(GCOL_Sample_Type)
#define GCOL_Pixel_Model GHOTIIO_COLOR(GCOL_Pixel_Model)
#define GCOL_Sample_Layout GHOTIIO_COLOR(GCOL_Sample_Layout)
#define GCOL_Transform GHOTIIO_COLOR(GCOL_Transform)

#define gcol_limits_default GHOTIIO_COLOR(gcol_limits_default)
#define gcol_icc_parse GHOTIIO_COLOR(gcol_icc_parse)
#define gcol_icc_free GHOTIIO_COLOR(gcol_icc_free)
#define gcol_icc_version GHOTIIO_COLOR(gcol_icc_version)
#define gcol_icc_class GHOTIIO_COLOR(gcol_icc_class)
#define gcol_icc_data_space GHOTIIO_COLOR(gcol_icc_data_space)
#define gcol_icc_pcs GHOTIIO_COLOR(gcol_icc_pcs)
#define gcol_icc_intent GHOTIIO_COLOR(gcol_icc_intent)
#define gcol_icc_description GHOTIIO_COLOR(gcol_icc_description)
#define gcol_icc_is_matrix_trc GHOTIIO_COLOR(gcol_icc_is_matrix_trc)
#define gcol_icc_profile_id GHOTIIO_COLOR(gcol_icc_profile_id)
#define gcol_icc_to_color_info GHOTIIO_COLOR(gcol_icc_to_color_info)
#define gcol_transform_create GHOTIIO_COLOR(gcol_transform_create)
#define gcol_transform_create_icc GHOTIIO_COLOR(gcol_transform_create_icc)
#define gcol_transform_create_to_icc GHOTIIO_COLOR(gcol_transform_create_to_icc)
#define gcol_transform_free GHOTIIO_COLOR(gcol_transform_free)
#define gcol_transform_apply GHOTIIO_COLOR(gcol_transform_apply)

/// @endcond

#endif /* GHOTI_IO_GCOL_NAMESPACE_H */
