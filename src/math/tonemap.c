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
 * BT.2390 PQ EETF (HDR→display peak mapping) in the PQ non-linear domain.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include <math.h>

/* ST 2084 constants (same as transfer_curve.c). */
static const double PQ_M1 = 2610.0 / 16384.0;
static const double PQ_M2 = 2523.0 / 4096.0 * 128.0;
static const double PQ_C1 = 3424.0 / 4096.0;
static const double PQ_C2 = 2413.0 / 4096.0 * 32.0;
static const double PQ_C3 = 2392.0 / 4096.0 * 32.0;

/** PQ OETF: normalised linear [0,1] of 10000 nits → non-linear [0,1]. */
static double pq_oetf(double n) {
  if (n < 0.0) {
    n = 0.0;
  }
  const double ym1 = pow(n, PQ_M1);
  const double num = PQ_C1 + PQ_C2 * ym1;
  const double den = 1.0 + PQ_C3 * ym1;
  return pow(num / den, PQ_M2);
}

/** PQ EOTF: non-linear [0,1] → normalised linear of 10000 nits. */
static double pq_eotf(double e) {
  if (e < 0.0) {
    e = 0.0;
  }
  if (e > 1.0) {
    e = 1.0;
  }
  const double ep = pow(e, 1.0 / PQ_M2);
  double num = ep - PQ_C1;
  if (num < 0.0) {
    num = 0.0;
  }
  const double den = PQ_C2 - PQ_C3 * ep;
  if (!(den > 0.0)) {
    return 0.0;
  }
  return pow(num / den, 1.0 / PQ_M1);
}

GCOL_API double gcol_tone_map_pq_eetf(double nits_in, double source_peak,
    double target_peak, double target_black) {
  if (!(source_peak > 0.0) || !(target_peak > 0.0)) {
    return nits_in;
  }
  if (target_black < 0.0) {
    target_black = 0.0;
  }
  if (target_black >= target_peak) {
    target_black = 0.0;
  }
  /* Already fits the target: identity in nits (aside from black lift). */
  if (source_peak <= target_peak && target_black <= 0.0) {
    if (nits_in < 0.0) {
      return 0.0;
    }
    return nits_in > target_peak ? target_peak : nits_in;
  }

  /* Mastering black LB = 0; LW = source_peak. Target Lmax / Lmin. */
  const double pq_lb = pq_oetf(0.0);
  const double pq_lw = pq_oetf(source_peak / 10000.0);
  const double pq_lmax = pq_oetf(target_peak / 10000.0);
  const double pq_lmin = pq_oetf(target_black / 10000.0);
  const double span = pq_lw - pq_lb;
  if (!(span > 0.0)) {
    return target_black;
  }

  double e = pq_oetf((nits_in < 0.0 ? 0.0 : nits_in) / 10000.0);
  double e1 = (e - pq_lb) / span;
  if (e1 < 0.0) {
    e1 = 0.0;
  }

  const double max_lum = (pq_lmax - pq_lb) / span;
  const double min_lum = (pq_lmin - pq_lb) / span;
  double ks = 1.5 * max_lum - 0.5;
  if (ks < 0.0) {
    ks = 0.0;
  }
  if (ks > 1.0) {
    ks = 1.0;
  }

  double e2;
  if (e1 < ks) {
    e2 = e1;
  } else if (!(1.0 - ks > 0.0)) {
    e2 = max_lum;
  } else {
    /* Hermite roll-off ending at max_lum with zero slope (BT.2390). */
    const double t = (e1 - ks) / (1.0 - ks);
    const double t2 = t * t;
    const double t3 = t2 * t;
    e2 = (2.0 * t3 - 3.0 * t2 + 1.0) * ks + (t3 - 2.0 * t2 + t) * (1.0 - ks) +
        (-2.0 * t3 + 3.0 * t2) * max_lum;
  }

  /* Optional black lift: E3 = E2 + minLum * (1 - E2)^4. */
  double e3 = e2;
  if (min_lum > 0.0) {
    const double om = 1.0 - e2;
    const double om2 = om * om;
    e3 = e2 + min_lum * (om2 * om2);
  }

  /* Denormalise with the mastering (source) PQ range — peak → target_peak. */
  const double e4 = e3 * span + pq_lb;
  return pq_eotf(e4) * 10000.0;
}
