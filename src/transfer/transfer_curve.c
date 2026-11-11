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
 * Transfer encode/decode for every GCOL_Transfer the description can name.
 *
 * sRGB uses the piecewise IEC 61966-2.1 form (not a pure 2.2 power). PQ is
 * ST 2084; HLG is BT.2100's OETF/inverse. Parametric uses the type-4 form
 * stored in transfer_params.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>

#include <float.h>
#include <math.h>

/* Not isfinite: the Windows headers' macro converts its argument to float on one path, which -Wfloat-conversion refuses. */
#define gcol_finite(x) (fabs((double) (x)) <= DBL_MAX)

/* ST 2084 constants. */
static const double gcol_pq_m1 = 2610.0 / 16384.0;
static const double gcol_pq_m2 = 2523.0 / 4096.0 * 128.0;
static const double gcol_pq_c1 = 3424.0 / 4096.0;
static const double gcol_pq_c2 = 2413.0 / 4096.0 * 32.0;
static const double gcol_pq_c3 = 2392.0 / 4096.0 * 32.0;

/* HLG constants (BT.2100). */
static const double gcol_hlg_a = 0.17883277;
static const double gcol_hlg_b = 0.28466892; /* 1 - 4*a */
static const double gcol_hlg_c = 0.55991073; /* 0.5 - a*ln(4*a) */

static double clamp01(double x) {
  if (x < 0.0) {
    return 0.0;
  }
  if (x > 1.0) {
    return 1.0;
  }
  return x;
}

static double srgb_decode(double e) {
  if (e <= 0.04045) {
    return e / 12.92;
  }
  return pow((e + 0.055) / 1.055, 2.4);
}

static double srgb_encode(double L) {
  if (L <= 0.0031308) {
    return L * 12.92;
  }
  return 1.055 * pow(L, 1.0 / 2.4) - 0.055;
}

static double pq_decode(double e) {
  /* EOTF: encoded in [0,1] → normalised linear [0,1] of 10000 cd/m^2. */
  e = clamp01(e);
  const double ep = pow(e, 1.0 / gcol_pq_m2);
  const double num = fmax(ep - gcol_pq_c1, 0.0);
  const double den = gcol_pq_c2 - gcol_pq_c3 * ep;
  if (!(den > 0.0)) {
    return 0.0;
  }
  return pow(num / den, 1.0 / gcol_pq_m1);
}

static double pq_encode(double L) {
  L = clamp01(L);
  const double Lm = pow(L, gcol_pq_m1);
  const double num = gcol_pq_c1 + gcol_pq_c2 * Lm;
  const double den = 1.0 + gcol_pq_c3 * Lm;
  return pow(num / den, gcol_pq_m2);
}

static double hlg_decode(double e) {
  /* Inverse OETF (scene-referred). */
  e = clamp01(e);
  if (e <= 0.5) {
    return (e * e) / 3.0;
  }
  return (exp((e - gcol_hlg_c) / gcol_hlg_a) + gcol_hlg_b) / 12.0;
}

static double hlg_encode(double L) {
  L = fmax(L, 0.0);
  if (L <= 1.0 / 12.0) {
    return sqrt(3.0 * L);
  }
  return gcol_hlg_a * log(12.0 * L - gcol_hlg_b) + gcol_hlg_c;
}

static double parametric_decode(const double p[GCOL_TRANSFER_PARAM_COUNT],
    double X) {
  /* Type 4: Y = (aX+b)^g + e for X >= d; Y = cX + f below. */
  const double g = p[0], a = p[1], b = p[2], c = p[3], d = p[4], e = p[5],
               f = p[6];
  if (X >= d) {
    const double t = a * X + b;
    if (t < 0.0 && g != floor(g)) {
      return 0.0;
    }
    return pow(t, g) + e;
  }
  return c * X + f;
}

static double parametric_encode(const double p[GCOL_TRANSFER_PARAM_COUNT],
    double Y) {
  /* Inverse of type 4, for the monotonic branch used by sRGB-like curves. */
  const double g = p[0], a = p[1], b = p[2], c = p[3], d = p[4], e = p[5],
               f = p[6];
  const double yd = parametric_decode(p, d);
  if (Y >= yd) {
    if (!(a > 0.0) || !(g > 0.0)) {
      return Y;
    }
    return (pow(Y - e, 1.0 / g) - b) / a;
  }
  if (!(fabs(c) > 1e-18)) {
    return Y;
  }
  return (Y - f) / c;
}

GCOL_API double gcol_transfer_decode(
    const GCOL_Color_Info * info, double encoded) {
  if (!info) {
    return encoded;
  }
  switch (info->transfer) {
  case GCOL_TRANSFER_LINEAR:
    return encoded;
  case GCOL_TRANSFER_SRGB:
    return srgb_decode(encoded);
  case GCOL_TRANSFER_GAMMA:
    if (!(info->gamma_value > 0.0) || !gcol_finite(info->gamma_value)) {
      return encoded;
    }
    if (encoded < 0.0) {
      return -pow(-encoded, info->gamma_value);
    }
    return pow(encoded, info->gamma_value);
  case GCOL_TRANSFER_PARAMETRIC:
    return parametric_decode(info->transfer_params, encoded);
  case GCOL_TRANSFER_BT1886:
    /* Reference: L = (V^2.4) with black=0; same as pure gamma 2.4. */
    if (encoded < 0.0) {
      return 0.0;
    }
    return pow(encoded, 2.4);
  case GCOL_TRANSFER_PQ:
    return pq_decode(encoded);
  case GCOL_TRANSFER_HLG:
    return hlg_decode(encoded);
  default:
    return encoded;
  }
}

GCOL_API double gcol_transfer_encode(
    const GCOL_Color_Info * info, double linear) {
  if (!info) {
    return linear;
  }
  switch (info->transfer) {
  case GCOL_TRANSFER_LINEAR:
    return linear;
  case GCOL_TRANSFER_SRGB:
    return srgb_encode(linear);
  case GCOL_TRANSFER_GAMMA:
    if (!(info->gamma_value > 0.0) || !gcol_finite(info->gamma_value)) {
      return linear;
    }
    if (linear < 0.0) {
      return -pow(-linear, 1.0 / info->gamma_value);
    }
    return pow(linear, 1.0 / info->gamma_value);
  case GCOL_TRANSFER_PARAMETRIC:
    return parametric_encode(info->transfer_params, linear);
  case GCOL_TRANSFER_BT1886:
    if (linear < 0.0) {
      return 0.0;
    }
    return pow(linear, 1.0 / 2.4);
  case GCOL_TRANSFER_PQ:
    return pq_encode(linear);
  case GCOL_TRANSFER_HLG:
    return hlg_encode(linear);
  default:
    return linear;
  }
}
