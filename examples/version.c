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
 * Prints what this build is, and one result string.
 *
 * The smallest program that links the library, so that `make examples` fails
 * if the installed headers and the shared library disagree.
 */

#include <ghoti.io/color/color.h>
#include <stdio.h>

int main(void) {
  printf("Ghoti.io Color %s\n", gcol_version_string());
  printf("%s\n", gcol_result_string(GCOL_ERR_UNSUPPORTED));
  return 0;
}
