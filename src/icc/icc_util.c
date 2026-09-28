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
 * Shared ICC helpers: byte readers, limits, tone curves.
 */

#include "icc_internal.h"

#include <ghoti.io/color/math.h>

#include <math.h>
#include <string.h>

uint32_t gcol_icc_read_u32(const unsigned char * p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint16_t gcol_icc_read_u16(const unsigned char * p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int32_t gcol_icc_read_s32(const unsigned char * p) {
  return (int32_t)gcol_icc_read_u32(p);
}

double gcol_icc_read_s15f16(const unsigned char * p) {
  return (double)gcol_icc_read_s32(p) / 65536.0;
}

void gcol_icc_limits_resolve(const GCOL_Limits * in, GCOL_Limits * out) {
  gcol_limits_default(out);
  if (!in) {
    return;
  }
  if (in->tag_count) {
    out->tag_count = in->tag_count;
  }
  if (in->curve_samples) {
    out->curve_samples = in->curve_samples;
  }
  if (in->profile_bytes) {
    out->profile_bytes = in->profile_bytes;
  }
  if (in->lut_grid) {
    out->lut_grid = in->lut_grid;
  }
}

void gcol_icc_curve_clear(gcol_icc_curve_t * c, const GCOL_Allocator * alloc) {
  if (!c) {
    return;
  }
  if (c->samples) {
    alloc->free_fn(alloc->ctx, c->samples);
    c->samples = NULL;
  }
  c->sample_count = 0;
  c->kind = GCOL_ICC_CURVE_IDENTITY;
  c->gamma = 1.0;
  c->para_function = 0;
  c->para_count = 0;
  memset(c->para, 0, sizeof(c->para));
}

static double clamp01(double v) {
  if (v < 0.0) {
    return 0.0;
  }
  if (v > 1.0) {
    return 1.0;
  }
  return v;
}

GCOL_Result gcol_icc_curve_decode(
    const gcol_icc_curve_t * c, double encoded, double * linear) {
  if (!c || !linear) {
    return GCOL_ERR_INVALID;
  }
  encoded = clamp01(encoded);
  switch (c->kind) {
  case GCOL_ICC_CURVE_IDENTITY:
    *linear = encoded;
    return GCOL_OK;
  case GCOL_ICC_CURVE_GAMMA:
    if (!(c->gamma > 0.0)) {
      return GCOL_ERR_CORRUPT;
    }
    *linear = pow(encoded, c->gamma);
    return GCOL_OK;
  case GCOL_ICC_CURVE_SAMPLED: {
    if (!c->samples || c->sample_count < 2) {
      return GCOL_ERR_CORRUPT;
    }
    double pos = encoded * (double)(c->sample_count - 1u);
    size_t i = (size_t)pos;
    if (i >= c->sample_count - 1u) {
      *linear = (double)c->samples[c->sample_count - 1u] / 65535.0;
      return GCOL_OK;
    }
    double t = pos - (double)i;
    double a = (double)c->samples[i] / 65535.0;
    double b = (double)c->samples[i + 1u] / 65535.0;
    *linear = a + t * (b - a);
    return GCOL_OK;
  }
  case GCOL_ICC_CURVE_PARAMETRIC: {
    /* ICC.1:2010 §10.15. Params: g,a,b,c,d,e,f depending on function. */
    const double * p = c->para;
    const double X = encoded;
    double Y = 0.0;
    switch (c->para_function) {
    case 0:
      Y = pow(X, p[0]);
      break;
    case 1:
      Y = (X >= -p[2] / p[1]) ? pow(p[1] * X + p[2], p[0]) : 0.0;
      break;
    case 2:
      Y = (X >= -p[2] / p[1]) ? pow(p[1] * X + p[2], p[0]) + p[3] : p[3];
      break;
    case 3:
      Y = (X >= p[4]) ? pow(p[1] * X + p[2], p[0]) : p[3] * X;
      break;
    case 4:
      Y = (X >= p[4]) ? pow(p[1] * X + p[2], p[0]) + p[5]
                      : p[3] * X + p[6];
      break;
    default:
      return GCOL_ERR_CORRUPT;
    }
    *linear = clamp01(Y);
    return GCOL_OK;
  }
  }
  return GCOL_ERR_INTERNAL;
}

GCOL_Result gcol_icc_curve_encode(
    const gcol_icc_curve_t * c, double linear, double * encoded) {
  if (!c || !encoded) {
    return GCOL_ERR_INVALID;
  }
  linear = clamp01(linear);
  switch (c->kind) {
  case GCOL_ICC_CURVE_IDENTITY:
    *encoded = linear;
    return GCOL_OK;
  case GCOL_ICC_CURVE_GAMMA:
    if (!(c->gamma > 0.0)) {
      return GCOL_ERR_CORRUPT;
    }
    *encoded = pow(linear, 1.0 / c->gamma);
    return GCOL_OK;
  case GCOL_ICC_CURVE_SAMPLED: {
    if (!c->samples || c->sample_count < 2) {
      return GCOL_ERR_CORRUPT;
    }
    /* Invert by scanning the monotonically increasing sample table. */
    double target = linear * 65535.0;
    if (target <= (double)c->samples[0]) {
      *encoded = 0.0;
      return GCOL_OK;
    }
    size_t last = c->sample_count - 1u;
    if (target >= (double)c->samples[last]) {
      *encoded = 1.0;
      return GCOL_OK;
    }
    size_t lo = 0;
    size_t hi = last;
    while (hi - lo > 1u) {
      size_t mid = lo + (hi - lo) / 2u;
      if ((double)c->samples[mid] <= target) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    double a = (double)c->samples[lo];
    double b = (double)c->samples[hi];
    double t = (b > a) ? (target - a) / (b - a) : 0.0;
    *encoded = ((double)lo + t) / (double)last;
    return GCOL_OK;
  }
  case GCOL_ICC_CURVE_PARAMETRIC: {
    /* Invert by bisection; the ICC functions are designed monotone. */
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 48; i++) {
      double mid = 0.5 * (lo + hi);
      double y = 0.0;
      GCOL_Result r = gcol_icc_curve_decode(c, mid, &y);
      if (r != GCOL_OK) {
        return r;
      }
      if (y < linear) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    *encoded = 0.5 * (lo + hi);
    return GCOL_OK;
  }
  }
  return GCOL_ERR_INTERNAL;
}

/** True when sampled values match the IEC 61966-2.1 decode curve. */
static bool samples_match_srgb(const uint16_t * samples, size_t n) {
  if (n < 2) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    double x = (double)i / (double)(n - 1u);
    double lin = x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
    long expect = (long)(lin * 65535.0 + 0.5);
    if (expect < 0) {
      expect = 0;
    }
    if (expect > 65535) {
      expect = 65535;
    }
    long got = (long)samples[i];
    long d = got - expect;
    if (d < 0) {
      d = -d;
    }
    /* Allow ±2 LSB for s15-scale rounding across generators. */
    if (d > 2) {
      return false;
    }
  }
  return true;
}

GCOL_Result gcol_icc_curve_to_transfer(
    const gcol_icc_curve_t * c, GCOL_Color_Info * info) {
  if (!c || !info) {
    return GCOL_ERR_INVALID;
  }
  switch (c->kind) {
  case GCOL_ICC_CURVE_IDENTITY:
    info->transfer = GCOL_TRANSFER_LINEAR;
    info->gamma_value = 1.0;
    return GCOL_OK;
  case GCOL_ICC_CURVE_GAMMA:
    if (fabs(c->gamma - 1.0) < 1e-6) {
      info->transfer = GCOL_TRANSFER_LINEAR;
      info->gamma_value = 1.0;
    } else {
      info->transfer = GCOL_TRANSFER_GAMMA;
      info->gamma_value = c->gamma;
    }
    return GCOL_OK;
  case GCOL_ICC_CURVE_SAMPLED:
    if (samples_match_srgb(c->samples, c->sample_count)) {
      info->transfer = GCOL_TRANSFER_SRGB;
      (void)gcol_transfer_conventions(GCOL_TRANSFER_SRGB, &info->reference,
          &info->sample_scale, &info->white_luminance);
      return GCOL_OK;
    }
    /* Arbitrary sampled curve: keep LINEAR here is wrong; leave UNKNOWN and
     * let a transform built from the profile's curves use the samples. */
    info->transfer = GCOL_TRANSFER_UNKNOWN;
    return GCOL_OK;
  case GCOL_ICC_CURVE_PARAMETRIC: {
    /* Expand ICC functions 0..4 into the type-4 param vector Color_Info holds. */
    info->transfer = GCOL_TRANSFER_PARAMETRIC;
    memset(info->transfer_params, 0, sizeof(info->transfer_params));
    const double * p = c->para;
    switch (c->para_function) {
    case 0: /* Y = X^g */
      info->transfer_params[0] = p[0];
      info->transfer_params[1] = 1.0;
      break;
    case 1: /* Y = (aX+b)^g for X >= -b/a, else 0 */
      info->transfer_params[0] = p[0];
      info->transfer_params[1] = p[1];
      info->transfer_params[2] = p[2];
      if (fabs(p[1]) > 1e-18) {
        info->transfer_params[4] = -p[2] / p[1];
      }
      break;
    case 2: /* Y = (aX+b)^g + c for X >= -b/a, else c */
      info->transfer_params[0] = p[0];
      info->transfer_params[1] = p[1];
      info->transfer_params[2] = p[2];
      info->transfer_params[5] = p[3]; /* e */
      info->transfer_params[6] = p[3]; /* f = c so below-threshold is constant c */
      if (fabs(p[1]) > 1e-18) {
        info->transfer_params[4] = -p[2] / p[1];
      }
      break;
    case 3: /* Y = (aX+b)^g for X >= d, else cX */
      info->transfer_params[0] = p[0];
      info->transfer_params[1] = p[1];
      info->transfer_params[2] = p[2];
      info->transfer_params[3] = p[3];
      info->transfer_params[4] = p[4];
      break;
    case 4: /* full type 4 */
      for (int i = 0; i < 7; i++) {
        info->transfer_params[i] = p[i];
      }
      break;
    default:
      return GCOL_ERR_CORRUPT;
    }
    return GCOL_OK;
  }
  }
  return GCOL_ERR_INTERNAL;
}
