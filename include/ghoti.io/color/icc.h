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
 * ICC profile synthesis (matrix/TRC) from a colour description.
 */

#ifndef GHOTI_IO_GCOL_ICC_H
#define GHOTI_IO_GCOL_ICC_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/color.h>
#include <ghoti.io/color/core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
