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
 * Relative-colorimetric RGB↔RGB transforms via XYZ D50.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>
#include <ghoti.io/color/transform.h>

#include "../math/matrix_internal.h"

#include <math.h>
#include <string.h>

struct GCOL_Transform {
  GCOL_Allocator alloc;
  GCOL_Color_Info src;
  GCOL_Color_Info dst;
  /** Combined linear-RGB src → linear-RGB dst under relative colorimetric. */
  double matrix[9];
  uint32_t flags;
};

static const GCOL_Chromaticity gcol_pcs_d50 = {0.3457, 0.3585};

static double clamp01(double v) {
  if (v < 0.0) {
    return 0.0;
  }
  if (v > 1.0) {
    return 1.0;
  }
  return v;
}

static bool layout_ok(const GCOL_Sample_Layout * layout) {
  if (!layout) {
    return false;
  }
  if (layout->type >= GCOL_SAMPLE_TYPE_COUNT) {
    return false;
  }
  return layout->channel_count == 3u || layout->channel_count == 4u;
}

static size_t sample_stride(const GCOL_Sample_Layout * layout) {
  size_t bytes = 0;
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    bytes = 1;
    break;
  case GCOL_SAMPLE_FLOAT32:
    bytes = 4;
    break;
  case GCOL_SAMPLE_FLOAT64:
    bytes = 8;
    break;
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
  return bytes * (size_t)layout->channel_count;
}

static void read_pixel(const GCOL_Sample_Layout * layout, const unsigned char * p,
    double rgb[3], double * alpha) {
  *alpha = 1.0;
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    rgb[0] = (double)p[0] / 255.0;
    rgb[1] = (double)p[1] / 255.0;
    rgb[2] = (double)p[2] / 255.0;
    if (layout->channel_count == 4u) {
      *alpha = (double)p[3] / 255.0;
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    const float * f = (const float *)(const void *)p;
    rgb[0] = (double)f[0];
    rgb[1] = (double)f[1];
    rgb[2] = (double)f[2];
    if (layout->channel_count == 4u) {
      *alpha = (double)f[3];
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    const double * d = (const double *)(const void *)p;
    rgb[0] = d[0];
    rgb[1] = d[1];
    rgb[2] = d[2];
    if (layout->channel_count == 4u) {
      *alpha = d[3];
    }
    break;
  }
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
}

static void write_pixel(const GCOL_Sample_Layout * layout, unsigned char * p,
    const double rgb[3], double alpha) {
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    /* Narrowing truncates. */
    p[0] = (unsigned char)(clamp01(rgb[0]) * 255.0);
    p[1] = (unsigned char)(clamp01(rgb[1]) * 255.0);
    p[2] = (unsigned char)(clamp01(rgb[2]) * 255.0);
    if (layout->channel_count == 4u) {
      p[3] = (unsigned char)(clamp01(alpha) * 255.0);
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    float * f = (float *)(void *)p;
    f[0] = (float)rgb[0];
    f[1] = (float)rgb[1];
    f[2] = (float)rgb[2];
    if (layout->channel_count == 4u) {
      f[3] = (float)alpha;
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    double * d = (double *)(void *)p;
    d[0] = rgb[0];
    d[1] = rgb[1];
    d[2] = rgb[2];
    if (layout->channel_count == 4u) {
      d[3] = alpha;
    }
    break;
  }
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
}

static GCOL_Result build_rgb_to_pcs(
    const GCOL_Color_Info * info, double out[9]) {
  double m[9];
  GCOL_Result r = gcol_gamut_to_xyz(&info->gamut, m);
  if (r != GCOL_OK) {
    return r;
  }
  double adapt[9];
  r = gcol_chromatic_adapt(
      info->gamut.white, gcol_pcs_d50, GCOL_CAT_BRADFORD, adapt);
  if (r != GCOL_OK) {
    return r;
  }
  gcol_mat3_mul(adapt, m, out);
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_transform_create(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, const GCOL_Color_Info * dst,
    GCOL_Rendering_Intent intent, uint32_t flags, GCOL_Transform ** out) {
  if (!out) {
    return GCOL_ERR_INVALID;
  }
  *out = NULL;
  if (!src || !dst) {
    return GCOL_ERR_INVALID;
  }
  if (!src->primaries_stated || !src->white_stated ||
      !dst->primaries_stated || !dst->white_stated) {
    return GCOL_ERR_INVALID;
  }
  if (src->transfer == GCOL_TRANSFER_UNKNOWN ||
      dst->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (intent != GCOL_INTENT_RELATIVE_COLORIMETRIC) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (!alloc) {
    alloc = gcol_allocator_default();
  }
  if (flags == 0u) {
    flags = GCOL_TRANSFORM_CLIP;
  }

  double src_to_pcs[9];
  double dst_to_pcs[9];
  GCOL_Result r = build_rgb_to_pcs(src, src_to_pcs);
  if (r != GCOL_OK) {
    return r;
  }
  r = build_rgb_to_pcs(dst, dst_to_pcs);
  if (r != GCOL_OK) {
    return r;
  }
  double pcs_to_dst[9];
  if (!gcol_mat3_inv(dst_to_pcs, pcs_to_dst)) {
    return GCOL_ERR_UNSUPPORTED;
  }

  GCOL_Transform * x =
      (GCOL_Transform *)alloc->calloc_fn(alloc->ctx, 1, sizeof(GCOL_Transform));
  if (!x) {
    return GCOL_ERR_OOM;
  }
  x->alloc = *alloc;
  x->src = *src;
  x->dst = *dst;
  x->src.icc_bytes = NULL;
  x->src.icc_size = 0;
  x->src.icc_linked_path = NULL;
  x->dst.icc_bytes = NULL;
  x->dst.icc_size = 0;
  x->dst.icc_linked_path = NULL;
  x->flags = flags;
  gcol_mat3_mul(pcs_to_dst, src_to_pcs, x->matrix);
  *out = x;
  return GCOL_OK;
}

GCOL_API void gcol_transform_free(GCOL_Transform * xform) {
  if (!xform) {
    return;
  }
  xform->alloc.free_fn(xform->alloc.ctx, xform);
}

GCOL_API GCOL_Result gcol_transform_apply(const GCOL_Transform * xform,
    const GCOL_Sample_Layout * src_layout, const void * src,
    const GCOL_Sample_Layout * dst_layout, void * dst, size_t pixel_count) {
  if (!xform || !src_layout || !dst_layout) {
    return GCOL_ERR_INVALID;
  }
  if (!layout_ok(src_layout) || !layout_ok(dst_layout)) {
    return GCOL_ERR_INVALID;
  }
  if (pixel_count && (!src || !dst)) {
    return GCOL_ERR_INVALID;
  }

  const size_t src_stride = sample_stride(src_layout);
  const size_t dst_stride = sample_stride(dst_layout);
  const unsigned char * sp = (const unsigned char *)src;
  unsigned char * dp = (unsigned char *)dst;
  const int clip = (xform->flags & GCOL_TRANSFORM_CLIP) != 0;

  for (size_t i = 0; i < pixel_count; i++) {
    double enc[3] = {0, 0, 0};
    double lin[3];
    double lin_dst[3];
    double enc_dst[3];
    double alpha = 1.0;
    read_pixel(src_layout, sp + i * src_stride, enc, &alpha);
    lin[0] = gcol_transfer_decode(&xform->src, enc[0]);
    lin[1] = gcol_transfer_decode(&xform->src, enc[1]);
    lin[2] = gcol_transfer_decode(&xform->src, enc[2]);
    gcol_mat3_vec(xform->matrix, lin, lin_dst);
    if (clip) {
      lin_dst[0] = clamp01(lin_dst[0]);
      lin_dst[1] = clamp01(lin_dst[1]);
      lin_dst[2] = clamp01(lin_dst[2]);
    }
    enc_dst[0] = gcol_transfer_encode(&xform->dst, lin_dst[0]);
    enc_dst[1] = gcol_transfer_encode(&xform->dst, lin_dst[1]);
    enc_dst[2] = gcol_transfer_encode(&xform->dst, lin_dst[2]);
    if (clip) {
      enc_dst[0] = clamp01(enc_dst[0]);
      enc_dst[1] = clamp01(enc_dst[1]);
      enc_dst[2] = clamp01(enc_dst[2]);
    }
    write_pixel(dst_layout, dp + i * dst_stride, enc_dst, alpha);
  }
  return GCOL_OK;
}
