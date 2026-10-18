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
 * Relative and absolute colorimetric transforms: RGB↔RGB via XYZ, ICC LUT/CMYK,
 * optional BPC and BT.2390 PQ EETF.
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
  /**
   * Matrix path: src linear RGB → PCS XYZ (D50 for relative; native for
   * absolute). LUT A2B path: PCS XYZ → dst linear RGB. LUT B2A path: src
   * linear RGB → PCS XYZ.
   */
  double src_to_pcs[9];
  double pcs_to_dst[9];
  /**
   * Absolute colorimetric scale applied to PCS XYZ after A2B / before B2A.
   * Relative intents leave this as (1,1,1). Absolute uses media_white / D50.
   */
  double abs_scale[3];
  /** BPC endpoints in PCS XYZ; identity when BPC is off or unavailable. */
  double bpc_src_black[3];
  double bpc_src_white[3];
  double bpc_dst_black[3];
  double bpc_dst_white[3];
  int bpc_enabled;
  /** PQ EETF: source/target peaks in nits; enabled when TONE_MAP + PQ abs. */
  int tone_map_enabled;
  double tone_src_peak;
  double tone_dst_peak;
  double tone_dst_black;
  uint32_t flags;
  GCOL_CMYK_Polarity cmyk_polarity;
  /** Borrowed from the profile passed to create_icc; not owned. */
  const gcol_icc_lut_t * lut;
  /** Borrowed profile for LUT BPC (bkpt); may be NULL. */
  const GCOL_ICC_Profile * icc_profile;
  uint8_t src_channels;
  uint8_t dst_channels;
};

static const GCOL_Chromaticity gcol_pcs_d50 = {0.3457, 0.3585};
/** ICC PCS illuminant XYZ (D50). */
static const double gcol_pcs_d50_xyz[3] = {0.9642, 1.0000, 0.8249};

static void abs_scale_from_profile(const GCOL_ICC_Profile * profile,
    GCOL_Rendering_Intent intent, double out[3]) {
  out[0] = out[1] = out[2] = 1.0;
  if (intent != GCOL_INTENT_ABSOLUTE_COLORIMETRIC || !profile) {
    return;
  }
  if (!profile->has_wtpt) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    if (gcol_pcs_d50_xyz[i] > 0.0) {
      out[i] = profile->wtpt[i] / gcol_pcs_d50_xyz[i];
    }
  }
}

static void bpc_identity(double black[3], double white[3]) {
  black[0] = black[1] = black[2] = 0.0;
  white[0] = gcol_pcs_d50_xyz[0];
  white[1] = gcol_pcs_d50_xyz[1];
  white[2] = gcol_pcs_d50_xyz[2];
}

/** Map PCS XYZ through source/dest black–white ranges (opt-in BPC). */
static void apply_bpc(const GCOL_Transform * x, double xyz[3]) {
  if (!x->bpc_enabled) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    const double span_s = x->bpc_src_white[i] - x->bpc_src_black[i];
    const double span_d = x->bpc_dst_white[i] - x->bpc_dst_black[i];
    if (span_s > 1e-12) {
      xyz[i] = (xyz[i] - x->bpc_src_black[i]) / span_s * span_d +
          x->bpc_dst_black[i];
    }
  }
}

/**
 * Chromaticity-preserving PQ EETF on PCS XYZ: remap Y in nits, scale XZ.
 * @p xyz is relative to the source peak (Y≈1 at source white); output is
 * relative to the destination peak (Y≈1 at destination white).
 */
static void apply_tone_map(const GCOL_Transform * x, double xyz[3]) {
  if (!x->tone_map_enabled) {
    return;
  }
  const double y = xyz[1];
  if (!(y > 0.0) || !(x->tone_dst_peak > 0.0)) {
    return;
  }
  const double nits_in = y * x->tone_src_peak;
  const double nits_out = gcol_tone_map_pq_eetf(
      nits_in, x->tone_src_peak, x->tone_dst_peak, x->tone_dst_black);
  const double y_out = nits_out / x->tone_dst_peak;
  const double s = y_out / y;
  xyz[0] *= s;
  xyz[1] = y_out;
  xyz[2] *= s;
}

/** Black as a fraction of white_luminance → PCS XYZ under D50. */
static void black_from_luminance(const GCOL_Color_Info * info, double black[3]) {
  black[0] = black[1] = black[2] = 0.0;
  if (info->black_luminance > 0.0 && info->white_luminance > 0.0) {
    const double f = info->black_luminance / info->white_luminance;
    black[0] = f * gcol_pcs_d50_xyz[0];
    black[1] = f * gcol_pcs_d50_xyz[1];
    black[2] = f * gcol_pcs_d50_xyz[2];
  }
}

static void setup_bpc_matrix(GCOL_Transform * x, const GCOL_Color_Info * src,
    const GCOL_Color_Info * dst, uint32_t flags) {
  bpc_identity(x->bpc_src_black, x->bpc_src_white);
  bpc_identity(x->bpc_dst_black, x->bpc_dst_white);
  x->bpc_enabled = 0;
  if ((flags & GCOL_TRANSFORM_BPC) == 0u) {
    return;
  }
  black_from_luminance(src, x->bpc_src_black);
  black_from_luminance(dst, x->bpc_dst_black);
  x->bpc_enabled = 1;
}

static void setup_bpc_icc_src(GCOL_Transform * x, const GCOL_ICC_Profile * src,
    const GCOL_Color_Info * dst, uint32_t flags) {
  bpc_identity(x->bpc_src_black, x->bpc_src_white);
  bpc_identity(x->bpc_dst_black, x->bpc_dst_white);
  x->bpc_enabled = 0;
  if ((flags & GCOL_TRANSFORM_BPC) == 0u) {
    return;
  }
  if (src->has_bkpt) {
    x->bpc_src_black[0] = src->bkpt[0];
    x->bpc_src_black[1] = src->bkpt[1];
    x->bpc_src_black[2] = src->bkpt[2];
  }
  if (src->has_wtpt) {
    x->bpc_src_white[0] = src->wtpt[0];
    x->bpc_src_white[1] = src->wtpt[1];
    x->bpc_src_white[2] = src->wtpt[2];
  }
  black_from_luminance(dst, x->bpc_dst_black);
  x->bpc_enabled = 1;
}

static void setup_bpc_icc_dst(GCOL_Transform * x, const GCOL_Color_Info * src,
    const GCOL_ICC_Profile * dst, uint32_t flags) {
  bpc_identity(x->bpc_src_black, x->bpc_src_white);
  bpc_identity(x->bpc_dst_black, x->bpc_dst_white);
  x->bpc_enabled = 0;
  if ((flags & GCOL_TRANSFORM_BPC) == 0u) {
    return;
  }
  black_from_luminance(src, x->bpc_src_black);
  if (dst->has_bkpt) {
    x->bpc_dst_black[0] = dst->bkpt[0];
    x->bpc_dst_black[1] = dst->bkpt[1];
    x->bpc_dst_black[2] = dst->bkpt[2];
  }
  if (dst->has_wtpt) {
    x->bpc_dst_white[0] = dst->wtpt[0];
    x->bpc_dst_white[1] = dst->wtpt[1];
    x->bpc_dst_white[2] = dst->wtpt[2];
  }
  x->bpc_enabled = 1;
}

static void setup_tone_map(GCOL_Transform * x, const GCOL_Color_Info * src,
    const GCOL_Color_Info * dst, uint32_t flags) {
  x->tone_map_enabled = 0;
  x->tone_src_peak = 0.0;
  x->tone_dst_peak = 0.0;
  x->tone_dst_black = 0.0;
  if ((flags & GCOL_TRANSFORM_TONE_MAP) == 0u) {
    return;
  }
  if (!src || src->sample_scale != GCOL_SAMPLE_SCALE_ABSOLUTE ||
      src->transfer != GCOL_TRANSFER_PQ) {
    return;
  }
  double src_peak = src->white_luminance;
  if (!(src_peak > 0.0)) {
    src_peak = 10000.0;
  }
  double dst_peak = 0.0;
  if (dst) {
    dst_peak = dst->white_luminance;
    if (!(dst_peak > 0.0)) {
      double conv = 0.0;
      (void)gcol_transfer_conventions(dst->transfer, NULL, NULL, &conv);
      dst_peak = conv > 0.0 ? conv : 100.0;
    }
    if (dst->black_luminance > 0.0) {
      x->tone_dst_black = dst->black_luminance;
    }
  } else {
    dst_peak = 100.0;
  }
  x->tone_map_enabled = 1;
  x->tone_src_peak = src_peak;
  x->tone_dst_peak = dst_peak;
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

static bool layout_ok_src(
    const GCOL_Sample_Layout * layout, const GCOL_Transform * x) {
  if (!layout || layout->type >= GCOL_SAMPLE_TYPE_COUNT) {
    return false;
  }
  if (layout->model == GCOL_PIXEL_CMYK) {
    return layout->channel_count == 4u && x->src_channels == 4u;
  }
  if (layout->model == GCOL_PIXEL_GRAY) {
    return x->src_channels == 1u &&
        (layout->channel_count == 1u || layout->channel_count == 2u);
  }
  if (layout->model != GCOL_PIXEL_RGB) {
    return false;
  }
  if (x->src_channels != 3u) {
    /* A CMYK profile needs a CMYK layout, a gray endpoint a gray one. */
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
  if (layout->model == GCOL_PIXEL_GRAY) {
    return x->dst_channels == 1u &&
        (layout->channel_count == 1u || layout->channel_count == 2u);
  }
  if (layout->model != GCOL_PIXEL_RGB || x->dst_channels != 3u) {
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
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      *alpha = (double)p[n] / 255.0;
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    const float * f = (const float *)(const void *)p;
    for (unsigned i = 0; i < n; i++) {
      ch[i] = (double)f[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      *alpha = (double)f[n];
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    const double * d = (const double *)(const void *)p;
    for (unsigned i = 0; i < n; i++) {
      ch[i] = d[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      *alpha = d[n];
    }
    break;
  }
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
}

/**
 * A unit-interval sample as the nearest 8-bit code.  Rounded, not truncated:
 * a code that comes back through the curves as 4.9999 is 5, and truncating
 * moved 44 of the 256 codes of an sRGB identity down by one.
 */
static unsigned char to_u8(double v) {
  return (unsigned char)(clamp01(v) * 255.0 + 0.5);
}

static void write_channels(const GCOL_Sample_Layout * layout, unsigned char * p,
    const double * ch, unsigned n, double alpha) {
  switch (layout->type) {
  case GCOL_SAMPLE_UINT8:
    for (unsigned i = 0; i < n; i++) {
      p[i] = to_u8(ch[i]);
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      p[n] = to_u8(alpha);
    }
    break;
  case GCOL_SAMPLE_FLOAT32: {
    float * f = (float *)(void *)p;
    for (unsigned i = 0; i < n; i++) {
      f[i] = (float)ch[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      f[n] = (float)alpha;
    }
    break;
  }
  case GCOL_SAMPLE_FLOAT64: {
    double * d = (double *)(void *)p;
    for (unsigned i = 0; i < n; i++) {
      d[i] = ch[i];
    }
    if (layout->model != GCOL_PIXEL_CMYK && layout->channel_count == n + 1u) {
      d[n] = alpha;
    }
    break;
  }
  case GCOL_SAMPLE_TYPE_COUNT:
    break;
  }
}

static GCOL_Result build_rgb_to_pcs(
    const GCOL_Color_Info * info, int absolute, double out[9]) {
  double m[9];
  GCOL_Result r = gcol_gamut_to_xyz(&info->gamut, m);
  if (r != GCOL_OK) {
    return r;
  }
  if (absolute) {
    /* Absolute colorimetric: keep native XYZ; do not Bradford-adapt to D50. */
    memcpy(out, m, sizeof(m));
    return GCOL_OK;
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

/**
 * A gray endpoint as a 3x3 on (lin, 0, 0): the white scaled by the sample
 * (ICC.1 F.2) - D50 under relative intent, the stated white under absolute.
 */
static GCOL_Result build_gray_to_pcs(
    const GCOL_Color_Info * info, int absolute, double out[9]) {
  double w[3];
  if (absolute) {
    const GCOL_Chromaticity c = info->gamut.white;
    if (!(c.y > 0.0)) {
      return GCOL_ERR_INVALID;
    }
    w[0] = c.x / c.y;
    w[1] = 1.0;
    w[2] = (1.0 - c.x - c.y) / c.y;
  }
  else {
    memcpy(w, gcol_pcs_d50_xyz, sizeof(w));
  }
  memset(out, 0, 9 * sizeof(double));
  out[0] = w[0];
  out[3] = w[1];
  out[6] = w[2];
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_transform_create(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, const GCOL_Color_Info * dst,
    GCOL_Rendering_Intent intent, uint32_t flags, GCOL_Transform ** out) {
  return gcol_transform_create_model(alloc, src, GCOL_PIXEL_RGB, dst,
      GCOL_PIXEL_RGB, intent, flags, out);
}

GCOL_API GCOL_Result gcol_transform_create_model(const GCOL_Allocator * alloc,
    const GCOL_Color_Info * src, GCOL_Pixel_Model src_model,
    const GCOL_Color_Info * dst, GCOL_Pixel_Model dst_model,
    GCOL_Rendering_Intent intent, uint32_t flags, GCOL_Transform ** out) {
  if (!out) {
    return GCOL_ERR_INVALID;
  }
  *out = NULL;
  if (!src || !dst) {
    return GCOL_ERR_INVALID;
  }
  if ((src_model != GCOL_PIXEL_RGB && src_model != GCOL_PIXEL_GRAY) ||
      (dst_model != GCOL_PIXEL_RGB && dst_model != GCOL_PIXEL_GRAY)) {
    return GCOL_ERR_INVALID;
  }
  const bool src_gray = src_model == GCOL_PIXEL_GRAY;
  const bool dst_gray = dst_model == GCOL_PIXEL_GRAY;
  // Gray needs a white and nothing else; RGB needs primaries too.
  if (!src->white_stated || !dst->white_stated ||
      (!src_gray && !src->primaries_stated) ||
      (!dst_gray && !dst->primaries_stated)) {
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

  const int absolute = (intent == GCOL_INTENT_ABSOLUTE_COLORIMETRIC);
  double src_to_pcs[9];
  GCOL_Result r = src_gray ? build_gray_to_pcs(src, absolute, src_to_pcs)
                           : build_rgb_to_pcs(src, absolute, src_to_pcs);
  if (r != GCOL_OK) {
    return r;
  }
  double pcs_to_dst[9];
  if (dst_gray) {
    // The PCS luminance, as a fraction of the destination's white: Y, since
    // both the D50 PCS white and a stated white have Y = 1.
    memset(pcs_to_dst, 0, sizeof(pcs_to_dst));
    pcs_to_dst[1] = 1.0;
  }
  else {
    double dst_to_pcs[9];
    r = build_rgb_to_pcs(dst, absolute, dst_to_pcs);
    if (r != GCOL_OK) {
      return r;
    }
    if (!gcol_mat3_inv(dst_to_pcs, pcs_to_dst)) {
      return GCOL_ERR_UNSUPPORTED;
    }
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
  x->src_channels = src_gray ? 1u : 3u;
  x->dst_channels = dst_gray ? 1u : 3u;
  x->lut = NULL;
  x->icc_profile = NULL;
  x->cmyk_polarity = GCOL_CMYK_POLARITY_UNKNOWN;
  x->abs_scale[0] = x->abs_scale[1] = x->abs_scale[2] = 1.0;
  memcpy(x->src_to_pcs, src_to_pcs, sizeof(x->src_to_pcs));
  memcpy(x->pcs_to_dst, pcs_to_dst, sizeof(x->pcs_to_dst));
  setup_bpc_matrix(x, src, dst, flags);
  setup_tone_map(x, src, dst, flags);
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
  const gcol_icc_lut_t * lut = gcol_icc_a2b_for_intent(src, intent);
  if (!lut) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (!dst->primaries_stated || !dst->white_stated ||
      dst->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_INVALID;
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

  /* LUT PCS is always D50-relative; destination matrix adapts to D50. */
  double dst_to_pcs[9];
  GCOL_Result r = build_rgb_to_pcs(dst, 0, dst_to_pcs);
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
  x->lut = lut;
  x->icc_profile = src;
  x->src_channels = lut->input_chan;
  x->dst_channels = 3;
  x->cmyk_polarity = cmyk_polarity;
  abs_scale_from_profile(src, intent, x->abs_scale);
  memset(x->src_to_pcs, 0, sizeof(x->src_to_pcs));
  memcpy(x->pcs_to_dst, pcs_to_dst, sizeof(x->pcs_to_dst));
  setup_bpc_icc_src(x, src, dst, flags);
  setup_tone_map(x, NULL, dst, flags); /* device→PCS has no PQ Color_Info */
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
  const gcol_icc_lut_t * lut = gcol_icc_b2a_for_intent(dst, intent);
  if (!lut) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (!src->primaries_stated || !src->white_stated ||
      src->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GCOL_ERR_INVALID;
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

  double src_to_pcs[9];
  GCOL_Result r = build_rgb_to_pcs(src, 0, src_to_pcs);
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
  x->lut = lut;
  x->icc_profile = dst;
  x->src_channels = 3;
  x->dst_channels = lut->output_chan;
  x->cmyk_polarity = cmyk_polarity;
  abs_scale_from_profile(dst, intent, x->abs_scale);
  memcpy(x->src_to_pcs, src_to_pcs, sizeof(x->src_to_pcs));
  memset(x->pcs_to_dst, 0, sizeof(x->pcs_to_dst));
  setup_bpc_icc_dst(x, src, dst, flags);
  setup_tone_map(x, src, NULL, flags);
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
  // An integer can only hold [0, 1], so eight-bit output is always clipped;
  // float output keeps whatever the coordinates come to - brighter than
  // white, outside the gamut - unless the caller asks for CLIP.  That is
  // littleCMS's convention for float transforms, and what keeps a transform
  // a change of coordinates rather than a tone map.
  const int clip = (xform->flags & GCOL_TRANSFORM_CLIP) != 0 ||
      dst_layout->type == GCOL_SAMPLE_UINT8;

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
      gcol_mat3_vec(xform->src_to_pcs, lin, xyz);
      apply_tone_map(xform, xyz);
      apply_bpc(xform, xyz);
      /* Absolute: undo media→D50 mapping before the relative B2A table. */
      xyz[0] /= xform->abs_scale[0];
      xyz[1] /= xform->abs_scale[1];
      xyz[2] /= xform->abs_scale[2];
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
      /* Absolute: re-apply media white after the relative A2B table. */
      xyz[0] *= xform->abs_scale[0];
      xyz[1] *= xform->abs_scale[1];
      xyz[2] *= xform->abs_scale[2];
      apply_bpc(xform, xyz);
      apply_tone_map(xform, xyz);
      gcol_mat3_vec(xform->pcs_to_dst, xyz, lin_dst);
    } else {
      double lin[3];
      lin[0] = decode_to_relative(&xform->src, ch[0]);
      lin[1] = decode_to_relative(&xform->src, ch[1]);
      lin[2] = decode_to_relative(&xform->src, ch[2]);
      double xyz[3];
      gcol_mat3_vec(xform->src_to_pcs, lin, xyz);
      apply_bpc(xform, xyz);
      apply_tone_map(xform, xyz);
      gcol_mat3_vec(xform->pcs_to_dst, xyz, lin_dst);
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
    write_channels(
        dst_layout, dp + i * dst_stride, enc_dst, xform->dst_channels, alpha);
  }
  return GCOL_OK;
}
