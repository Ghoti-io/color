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
 * @file color.h
 *
 * Umbrella header for the Ghoti.io Color library.
 *
 * A colour engine: it describes a colour space, parses an ICC profile, and
 * transforms samples between two spaces.  It is deliberately not a colour
 * *management* system - no display profiling, no device calibration, no
 * monitor LUT loading.  The line is that this library answers "what does this
 * number mean, and what is it in that space", not "what is this particular
 * monitor doing today".
 *
 * Nothing but the scaffolding is declared yet.  The description types move
 * here from `libs/image` next, and the ICC parser and the transform follow;
 * planning/color.md in the workspace has the phases.  A header appears here in
 * the commit that implements it, so this list is what exists rather than what
 * is intended.
 */

#ifndef GHOTI_IO_GCOL_COLOR_H
#define GHOTI_IO_GCOL_COLOR_H

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/core.h>
#include <ghoti.io/color/macros.h>

#endif /* GHOTI_IO_GCOL_COLOR_H */
