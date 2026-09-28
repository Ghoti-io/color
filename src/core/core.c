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
 * @file
 *
 * Result strings.
 *
 * The string table is indexed by the enum. A new result that is not added
 * here falls through to "Unknown error", and the unit test that walks
 * GCOL_RESULT_COUNT rejects that.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/core.h>

#include <string.h>

const char * gcol_result_string(GCOL_Result result) {
  switch (result) {
    case GCOL_OK:
      return "No error";
    case GCOL_ERR_IO:
      return "Input/output error";
    case GCOL_ERR_FORMAT:
      return "Format not recognised";
    case GCOL_ERR_UNSUPPORTED:
      return "Unsupported feature";
    case GCOL_ERR_LIMIT:
      return "Limit exceeded";
    case GCOL_ERR_CORRUPT:
      return "Corrupt input";
    case GCOL_ERR_OOM:
      return "Out of memory";
    case GCOL_ERR_INVALID:
      return "Invalid argument";
    case GCOL_ERR_INTERNAL:
      return "Internal error";
    case GCOL_RESULT_COUNT:
      break;
  }
  return "Unknown error";
}

GCOL_API void gcol_limits_default(GCOL_Limits * limits) {
  if (!limits) {
    return;
  }
  memset(limits, 0, sizeof(*limits));
  limits->tag_count = GCOL_LIMITS_DEFAULT_TAG_COUNT;
  limits->curve_samples = GCOL_LIMITS_DEFAULT_CURVE_SAMPLES;
  limits->profile_bytes = GCOL_LIMITS_DEFAULT_PROFILE_BYTES;
  limits->lut_grid = GCOL_LIMITS_DEFAULT_LUT_GRID;
}
