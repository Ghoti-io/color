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
 * Relative-colorimetric transforms: RGB↔RGB via XYZ D50, and ICC LUT/CMYK.
 */

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/math.h>
#include <ghoti.io/color/transform.h>

#include "../icc/icc_internal.h"
#include "../icc/icc_lut.h"
#include "../math/matrix_internal.h"

#include <math.h>
#include <string.h>

enum {
  GCOL_XFORM_MATRIX = 0,
  GCOL_XFORM_ICC_LUT = 1,  ///< Device→PCS→RGB destination.
  GCOL_XFORM_ICC_B2A = 2   ///< RGB→PCS→device (BToA / destination CMYK).
};

struct GCOL_Transform {
  GCOL_Allocator alloc;
  GCOL_Color_Info src;
  GCOL_Color_Info dst;
  int kind;
  /** Combined linear-RGB src → linear-RGB dst under relative colorimetric. */
  double matrix[9];
  uint32_t flags;
  GCOL_CMYK_Polarity cmyk_polarity;
  /** Borrowed from the profile passed to create_icc; not owned. */
  const gcol_icc_lut_t * lut;
  uint8_t src_channels; ///< 3 for RGB matrix, lut->input_chan for ICC.
  uint8_t dst_channels; ///< 3 for RGB, lut->output_chan for B2A.
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

static bool layout_ok_src(
    const GCOL_Sample_Layout * layout, const GCOL_Transform * x) {
  if (!layout || layout->type >= GCOL_SAMPLE_TYPE_COUNT) {
    return false;
  }
  if (layout->model == GCOL_PIXEL_CMYK) {
    return layout->channel_count == 4u && x->src_channels == 4u;
  }
  if (layout->model != GCOL_PIXEL_RGB && layout->model != 0) {
    return false;
  }
  if (x->src_channels == 4u) {
    /* ICC CMYK source requires CMYK layout. */
    return false;
  }
  return layout->channel_count == 3u || layout->channel_count == 4u;
}

static bool layout_ok_dst(
    const GCOL_Sample_Layout * layout, const GCOL_Transform * x) {
  if (!layout || layout->type >= GCOL_SAMPLE_TYPE_COUNT) {
    return false;
  }
  if (layout->model == GCOL_PIXEL_CMYK) {
    return layout->channel_count == 4u && x->dst_channels == 4u;
  }
  if (x->dst_channels == 4u) {
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

static void read_channels(const GCOL_Sample_Layout * layout,
    const unsigned char * p, double * ch, unsigned n, double * alpha) {
  *alpha = 1.0;
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    for (unsigned i = 0; i < n; i++) {
      ch[i] = (double)p[i] / 255.0;
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u) {
      *alpha = (double)p[3] / 255.0;
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    const float * f = (const float *)(const void *)p;
    for (unsigned i = 0; i < n; i++) {
      ch[i] = (double)f[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u) {
      *alpha = (double)f[3];
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    const double * d = (const double *)(const void *)p;
    for (unsigned i = 0; i < n; i++) {
      ch[i] = d[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u) {
      *alpha = d[3];
    }
    break;
  }
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
}

static void write_channels(const GCOL_Sample_Layout * layout, unsigned char * p,
    const double * ch, unsigned n, double alpha) {
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    for (unsigned i = 0; i < n; i++) {
      p[i] = (unsigned char)(clamp01(ch[i]) * 255.0);
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u &&
        n < 4u) {
      p[3] = (unsigned char)(clamp01(alpha) * 255.0);
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    float * f = (float *)(void *)p;
    for (unsigned i = 0; i < n; i++) {
      f[i] = (float)ch[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u &&
        n < 4u) {
      f[3] = (float)alpha;
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    double * d = (double *)(void *)p;
    for (unsigned i = 0; i < n; i++) {
      d[i] = ch[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == 4u &&
        n < 4u) {
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

/**
 * Decode transfer to relative linear light in [0, ~1], honouring absolute PQ.
 * Absolute samples become relative by dividing by white_luminance (or the
 * transfer's conventional peak). That is the display-referred absolute→relative
 * tone map for this phase: a hard scale, not a filmic curve.
 */
static double decode_to_relative(const GCOL_Color_Info * info, double encoded) {
  double lin = gcol_transfer_decode(info, encoded);
  if (info->sample_scale == GCOL_SAMPLE_SCALE_ABSOLUTE) {
    double peak = info->white_luminance;
    if (!(peak > 0.0)) {
      double conv = 0.0;
      (void)gcol_transfer_conventions(
          info->transfer, NULL, NULL, &conv);
      peak = conv > 0.0 ? conv : 1.0;
    }
    /* PQ decode yields normalised [0,1] of 10000 nits; scale to nits then
     * to relative of the stated white. */
    if (info->transfer == GCOL_TRANSFER_PQ) {
      const double nits = lin * 10000.0;
      lin = nits / peak;
    } else {
      lin = lin; /* already relative-looking; white_luminance documents it */
      (void)peak;
    }
  }
  return lin;
}

static double encode_from_relative(
    const GCOL_Color_Info * info, double linear) {
  if (info->sample_scale == GCOL_SAMPLE_SCALE_ABSOLUTE &&
      info->transfer == GCOL_TRANSFER_PQ) {
    double peak = info->white_luminance;
    if (!(peak > 0.0)) {
      peak = 10000.0;
    }
    const double nits = linear * peak;
    linear = nits / 10000.0;
  }
  return gcol_transfer_encode(info, linear);
}

static void copy_info(GCOL_Color_Info * dst, const GCOL_Color_Info * src) {
  *dst = *src;
  dst->icc_bytes = NULL;
  dst->icc_size = 0;
  dst->icc_linked_path = NULL;
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
  if (!src->primaries_stated || !src->white_stated || !dst->primaries_stated ||
      !dst->white_stated) {
    return GCOL_ERR_INVALID;
  }
  if (src->transfer == GCOL_TRANSFER_UNKNOWN ||
      dst->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (intent != GCOL_INTENT_RELATIVE_COLORIMETRIC &&
      intent != GCOL_INTENT_ABSOLUTE_COLORIMETRIC) {
    /* Perceptual / saturation: vendor-defined; not implemented. */
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
  copy_info(&x->src, src);
  copy_info(&x->dst, dst);
  x->kind = GCOL_XFORM_MATRIX;
  x->flags = flags;
  x->src_channels = 3;
  x->dst_channels = 3;
  x->lut = NULL;
  x->cmyk_polarity = GCOL_CMYK_POLARITY_UNKNOWN;
  gcol_mat3_mul(pcs_to_dst, src_to_pcs, x->matrix);
  *out = x;
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_transform_create_icc(const GCOL_Allocator * alloc,
    const GCOL_ICC_Profile * src, const GCOL_Color_Info * dst,
    GCOL_Rendering_Intent intent, uint32_t flags,
    GCOL_CMYK_Polarity cmyk_polarity, GCOL_Transform ** out) {
  if (!out) {
    return GCOL_ERR_INVALID;
  }
  *out = NULL;
  if (!src || !dst) {
    return GCOL_ERR_INVALID;
  }
  if (!gcol_icc_has_lut(src)) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (!dst->primaries_stated || !dst->white_stated ||
      dst->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_INVALID;
  }
  if (intent != GCOL_INTENT_RELATIVE_COLORIMETRIC) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (src->data_space == GCOL_ICC_SIG_CMYK) {
    if (cmyk_polarity != GCOL_CMYK_POLARITY_INK &&
        cmyk_polarity != GCOL_CMYK_POLARITY_REFLECTION) {
      return GCOL_ERR_INVALID;
    }
  }
  if (!alloc) {
    alloc = gcol_allocator_default();
  }
  if (flags == 0u) {
    flags = GCOL_TRANSFORM_CLIP;
  }

  double dst_to_pcs[9];
  GCOL_Result r = build_rgb_to_pcs(dst, dst_to_pcs);
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
  gcol_color_info_default(&x->src);
  copy_info(&x->dst, dst);
  x->kind = GCOL_XFORM_ICC_LUT;
  x->flags = flags;
  x->lut = src->a2b;
  x->src_channels = src->a2b->input_chan;
  x->dst_channels = 3;
  x->cmyk_polarity = cmyk_polarity;
  memcpy(x->matrix, pcs_to_dst, sizeof(x->matrix));
  *out = x;
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_transform_create_to_icc(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, const GCOL_ICC_Profile * dst,
    GCOL_Rendering_Intent intent, uint32_t flags,
    GCOL_CMYK_Polarity cmyk_polarity, GCOL_Transform ** out) {
  if (!out) {
    return GCOL_ERR_INVALID;
  }
  *out = NULL;
  if (!src || !dst) {
    return GCOL_ERR_INVALID;
  }
  if (!gcol_icc_has_b2a(dst)) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (!src->primaries_stated || !src->white_stated ||
      src->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_INVALID;
  }
  if (intent != GCOL_INTENT_RELATIVE_COLORIMETRIC) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (dst->data_space == GCOL_ICC_SIG_CMYK) {
    if (cmyk_polarity != GCOL_CMYK_POLARITY_INK &&
        cmyk_polarity != GCOL_CMYK_POLARITY_REFLECTION) {
      return GCOL_ERR_INVALID;
    }
  }
  if (!alloc) {
    alloc = gcol_allocator_default();
  }
  if (flags == 0u) {
    flags = GCOL_TRANSFORM_CLIP;
  }

  double src_to_pcs[9];
  GCOL_Result r = build_rgb_to_pcs(src, src_to_pcs);
  if (r != GCOL_OK) {
    return r;
  }

  GCOL_Transform * x =
      (GCOL_Transform *)alloc->calloc_fn(alloc->ctx, 1, sizeof(GCOL_Transform));
  if (!x) {
    return GCOL_ERR_OOM;
  }
  x->alloc = *alloc;
  copy_info(&x->src, src);
  gcol_color_info_default(&x->dst);
  x->kind = GCOL_XFORM_ICC_B2A;
  x->flags = flags;
  x->lut = dst->b2a;
  x->src_channels = 3;
  x->dst_channels = dst->b2a->output_chan;
  x->cmyk_polarity = cmyk_polarity;
  memcpy(x->matrix, src_to_pcs, sizeof(x->matrix));
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
  if (!layout_ok_src(src_layout, xform) || !layout_ok_dst(dst_layout, xform)) {
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
    double ch[4] = {0, 0, 0, 0};
    double alpha = 1.0;
    const unsigned nsrc = xform->src_channels;
    read_channels(src_layout, sp + i * src_stride, ch, nsrc, &alpha);

    if (xform->kind == GCOL_XFORM_ICC_B2A) {
      double lin[3];
      lin[0] = decode_to_relative(&xform->src, ch[0]);
      lin[1] = decode_to_relative(&xform->src, ch[1]);
      lin[2] = decode_to_relative(&xform->src, ch[2]);
      double xyz[3];
      gcol_mat3_vec(xform->matrix, lin, xyz);
      double device[4] = {0, 0, 0, 0};
      GCOL_Result lr = gcol_icc_lut_apply_b2a(xform->lut, xyz, device);
      if (lr != GCOL_OK) {
        return lr;
      }
      if (dst_layout->model == GCOL_PIXEL_CMYK &&
          xform->cmyk_polarity == GCOL_CMYK_POLARITY_INK) {
        for (unsigned c = 0; c < xform->dst_channels; c++) {
          device[c] = 1.0 - device[c];
        }
      }
      if (clip) {
        for (unsigned c = 0; c < xform->dst_channels; c++) {
          device[c] = clamp01(device[c]);
        }
      }
      write_channels(
          dst_layout, dp + i * dst_stride, device, xform->dst_channels, alpha);
      continue;
    }

    double lin_dst[3];
    double enc_dst[3];
    if (xform->kind == GCOL_XFORM_ICC_LUT) {
      double device[4];
      for (unsigned c = 0; c < nsrc; c++) {
        double v = clamp01(ch[c]);
        if (src_layout->model == GCOL_PIXEL_CMYK &&
            xform->cmyk_polarity == GCOL_CMYK_POLARITY_INK) {
          v = 1.0 - v;
        }
        device[c] = v;
      }
      double xyz[3];
      GCOL_Result lr = gcol_icc_lut_apply(xform->lut, device, xyz);
      if (lr != GCOL_OK) {
        return lr;
      }
      gcol_mat3_vec(xform->matrix, xyz, lin_dst);
    } else {
      double lin[3];
      lin[0] = decode_to_relative(&xform->src, ch[0]);
      lin[1] = decode_to_relative(&xform->src, ch[1]);
      lin[2] = decode_to_relative(&xform->src, ch[2]);
      gcol_mat3_vec(xform->matrix, lin, lin_dst);
    }

    if (clip) {
      lin_dst[0] = clamp01(lin_dst[0]);
      lin_dst[1] = clamp01(lin_dst[1]);
      lin_dst[2] = clamp01(lin_dst[2]);
    }
    enc_dst[0] = encode_from_relative(&xform->dst, lin_dst[0]);
    enc_dst[1] = encode_from_relative(&xform->dst, lin_dst[1]);
    enc_dst[2] = encode_from_relative(&xform->dst, lin_dst[2]);
    if (clip) {
      enc_dst[0] = clamp01(enc_dst[0]);
      enc_dst[1] = clamp01(enc_dst[1]);
      enc_dst[2] = clamp01(enc_dst[2]);
    }
    write_channels(dst_layout, dp + i * dst_stride, enc_dst, 3, alpha);
  }
  return GCOL_OK;
}
