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

/// @endcond

#endif /* GHOTI_IO_GCOL_NAMESPACE_H */
