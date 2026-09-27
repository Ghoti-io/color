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
 * @file core.h
 *
 * Result codes and the version this build reports.
 *
 * There is no GCOL_Limits here yet, and its absence is deliberate.
 * CONVENTIONS.md section 5 says every parser takes one, and this library will
 * have a parser - an ICC profile is the obvious unbounded input, with a tag
 * count, a curve sample count and a LUT grid size all worth capping.  None of
 * those is capped today because nothing parses yet, and a cap nothing checks
 * is not a promise.  The struct arrives in the commit that enforces its first
 * field.  See documentation/design.md.
 */

#ifndef GHOTI_IO_GCOL_CORE_H
#define GHOTI_IO_GCOL_CORE_H

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/macros.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of an operation.
 *
 * Zero is success.  ::GCOL_RESULT_COUNT closes the enum so a test can check
 * the string table is complete.
 *
 * This is the suite's fixed vocabulary with nothing added.  A colour
 * conversion that cannot be performed is not a new kind of failure: a profile
 * this library cannot yet interpret is ::GCOL_ERR_UNSUPPORTED, a profile whose
 * bytes contradict its own tag table is ::GCOL_ERR_CORRUPT, and a caller who
 * asks for a transform between a space and itself has stated something valid.
 */
typedef enum {
  GCOL_OK = 0,          ///< The operation succeeded.
  GCOL_ERR_IO,          ///< A read, write, or seek failed.
  GCOL_ERR_FORMAT,      ///< Well-formed bytes, but not a format handled here.
  GCOL_ERR_UNSUPPORTED, ///< The format is known and the feature is not
                        ///< implemented.
  GCOL_ERR_LIMIT,       ///< A stated cap was exceeded.
  GCOL_ERR_CORRUPT,     ///< The bytes are not a valid encoding of this format.
  GCOL_ERR_OOM,         ///< The allocator returned NULL.
  GCOL_ERR_INVALID,     ///< A caller-supplied argument is wrong.
  GCOL_ERR_INTERNAL,    ///< The library's own invariant failed.
  GCOL_RESULT_COUNT
} GCOL_Result;

/**
 * @brief Static description of a result code.
 *
 * The string is never NULL, never allocated, and never contains caller data.
 *
 * @param result The result code, including values outside the enum.
 * @return A static string.
 */
GCOL_API const char * gcol_result_string(GCOL_Result result);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL.  "0.0.0", or "0.0.0-dev" when BRANCH
 *   was overridden.
 */
GCOL_API const char * gcol_version_string(void);

/**
 * @brief This build's version, packed as ::GCOL_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GCOL_API unsigned gcol_version_number(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GCOL_CORE_H */
