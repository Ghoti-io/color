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
  }
  return GCOL_ERR_INTERNAL;
}
