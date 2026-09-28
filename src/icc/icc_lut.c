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
 * ICC LUT tag parse and apply (mft1, mft2, mAB , mBA ).
 *
 * Colorimetric intents only: the CLUT is applied as stored. Perceptual intent
 * has no specification; agreeing with a named CMM is deferred (see design.md).
 */

#include "icc_lut.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define GCOL_ICC_SIG_MFT1 GCOL_ICC_SIG('m', 'f', 't', '1')
#define GCOL_ICC_SIG_MFT2 GCOL_ICC_SIG('m', 'f', 't', '2')
#define GCOL_ICC_SIG_MAB GCOL_ICC_SIG('m', 'A', 'B', ' ')
#define GCOL_ICC_SIG_MBA GCOL_ICC_SIG('m', 'B', 'A', ' ')
#define GCOL_ICC_SIG_CURV_TYPE GCOL_ICC_SIG('c', 'u', 'r', 'v')
#define GCOL_ICC_SIG_PARA GCOL_ICC_SIG('p', 'a', 'r', 'a')

static void * gcol_alloc(const GCOL_Allocator * a, size_t n, size_t sz) {
  return a->calloc_fn(a->ctx, n, sz);
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

void gcol_icc_lut_clear(gcol_icc_lut_t * lut, const GCOL_Allocator * alloc) {
  if (!lut) {
    return;
  }
  if (lut->in_curves) {
    for (uint8_t i = 0; i < lut->input_chan; i++) {
      gcol_icc_curve_clear(&lut->in_curves[i], alloc);
    }
    alloc->free_fn(alloc->ctx, lut->in_curves);
  }
  if (lut->m_curves) {
    for (uint8_t i = 0; i < lut->output_chan; i++) {
      gcol_icc_curve_clear(&lut->m_curves[i], alloc);
    }
    alloc->free_fn(alloc->ctx, lut->m_curves);
  }
  if (lut->out_curves) {
    for (uint8_t i = 0; i < lut->output_chan; i++) {
      gcol_icc_curve_clear(&lut->out_curves[i], alloc);
    }
    alloc->free_fn(alloc->ctx, lut->out_curves);
  }
  if (lut->clut) {
    alloc->free_fn(alloc->ctx, lut->clut);
  }
  memset(lut, 0, sizeof(*lut));
  lut->e[0] = lut->e[4] = lut->e[8] = 1.0;
}

static GCOL_Result read_curv_bytes(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * p, size_t avail,
    gcol_icc_curve_t * out, size_t * consumed) {
  if (avail < 12u) {
    return GCOL_ERR_CORRUPT;
  }
  uint32_t sig = gcol_icc_read_u32(p);
  if (sig == GCOL_ICC_SIG_CURV_TYPE) {
    uint32_t count = gcol_icc_read_u32(p + 8);
    if ((size_t)count > lim->curve_samples) {
      return GCOL_ERR_LIMIT;
    }
    size_t need = (count == 1u) ? 14u : (12u + (size_t)count * 2u);
    if (need > avail) {
      return GCOL_ERR_CORRUPT;
    }
    gcol_icc_curve_clear(out, alloc);
    if (count == 0u) {
      out->kind = GCOL_ICC_CURVE_IDENTITY;
      out->gamma = 1.0;
    } else if (count == 1u) {
      out->kind = GCOL_ICC_CURVE_GAMMA;
      out->gamma = (double)gcol_icc_read_u16(p + 12) / 256.0;
      if (!(out->gamma > 0.0)) {
        return GCOL_ERR_CORRUPT;
      }
    } else {
      out->kind = GCOL_ICC_CURVE_SAMPLED;
      out->sample_count = count;
      out->samples = (uint16_t *)gcol_alloc(alloc, count, sizeof(uint16_t));
      if (!out->samples) {
        return GCOL_ERR_OOM;
      }
      for (uint32_t i = 0; i < count; i++) {
        out->samples[i] = gcol_icc_read_u16(p + 12 + (size_t)i * 2u);
      }
    }
    /* Tags are 4-byte aligned. */
    *consumed = (need + 3u) & ~3u;
    if (*consumed > avail) {
      *consumed = need;
    }
    return GCOL_OK;
  }
  if (sig == GCOL_ICC_SIG_PARA) {
    /* parametricCurveType: function uInt16, reserved, then s15Fixed16 params. */
    if (avail < 12u) {
      return GCOL_ERR_CORRUPT;
    }
    uint16_t fn = gcol_icc_read_u16(p + 8);
    static const uint8_t k_params[5] = {1, 3, 4, 5, 7};
    if (fn > 4u) {
      return GCOL_ERR_CORRUPT;
    }
    uint8_t np = k_params[fn];
    size_t need = 12u + (size_t)np * 4u;
    if (avail < need) {
      return GCOL_ERR_CORRUPT;
    }
    gcol_icc_curve_clear(out, alloc);
    out->kind = GCOL_ICC_CURVE_PARAMETRIC;
    out->para_function = fn;
    out->para_count = np;
    for (uint8_t i = 0; i < np; i++) {
      out->para[i] = gcol_icc_read_s15f16(p + 12 + (size_t)i * 4u);
    }
    *consumed = (need + 3u) & ~3u;
    if (*consumed > avail) {
      *consumed = need;
    }
    return GCOL_OK;
  }
  return GCOL_ERR_CORRUPT;
}

static GCOL_Result alloc_identity_curves(const GCOL_Allocator * alloc,
    uint8_t n, gcol_icc_curve_t ** out) {
  gcol_icc_curve_t * c =
      (gcol_icc_curve_t *)gcol_alloc(alloc, n, sizeof(gcol_icc_curve_t));
  if (!c) {
    return GCOL_ERR_OOM;
  }
  for (uint8_t i = 0; i < n; i++) {
    c[i].kind = GCOL_ICC_CURVE_IDENTITY;
    c[i].gamma = 1.0;
  }
  *out = c;
  return GCOL_OK;
}

static size_t clut_len(uint8_t grid, uint8_t in_ch, uint8_t out_ch) {
  size_t n = 1;
  for (uint8_t i = 0; i < in_ch; i++) {
    if (n > (SIZE_MAX / (size_t)grid)) {
      return 0;
    }
    n *= (size_t)grid;
  }
  if (n > (SIZE_MAX / (size_t)out_ch)) {
    return 0;
  }
  return n * (size_t)out_ch;
}

static GCOL_Result parse_mft(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * tag, size_t tag_size,
    int is16, bool pcs_lab, gcol_icc_lut_t * out) {
  if (tag_size < 48u) {
    return GCOL_ERR_CORRUPT;
  }
  uint8_t in_ch = tag[8];
  uint8_t out_ch = tag[9];
  uint8_t grid = tag[10];
  if (in_ch < 1u || in_ch > 4u || out_ch < 1u || out_ch > 4u) {
    return GCOL_ERR_UNSUPPORTED;
  }
  if (grid < 2u) {
    return GCOL_ERR_CORRUPT;
  }
  if ((size_t)grid > lim->lut_grid) {
    return GCOL_ERR_LIMIT;
  }

  gcol_icc_lut_clear(out, alloc);
  out->input_chan = in_ch;
  out->output_chan = out_ch;
  out->grid = grid;
  out->pcs_lab = pcs_lab;
  out->has_matrix = true;
  for (int i = 0; i < 9; i++) {
    out->e[i] = gcol_icc_read_s15f16(tag + 12 + (size_t)i * 4u);
  }

  size_t in_entries;
  size_t out_entries;
  size_t pos;
  if (is16) {
    if (tag_size < 52u) {
      return GCOL_ERR_CORRUPT;
    }
    in_entries = gcol_icc_read_u16(tag + 48);
    out_entries = gcol_icc_read_u16(tag + 50);
    if (in_entries < 2u || out_entries < 2u) {
      return GCOL_ERR_CORRUPT;
    }
    if (in_entries > lim->curve_samples || out_entries > lim->curve_samples) {
      return GCOL_ERR_LIMIT;
    }
    pos = 52u;
  } else {
    in_entries = 256u;
    out_entries = 256u;
    pos = 48u;
  }

  GCOL_Result r = alloc_identity_curves(alloc, in_ch, &out->in_curves);
  if (r != GCOL_OK) {
    gcol_icc_lut_clear(out, alloc);
    return r;
  }
  r = alloc_identity_curves(alloc, out_ch, &out->out_curves);
  if (r != GCOL_OK) {
    gcol_icc_lut_clear(out, alloc);
    return r;
  }

  /* Input tables. */
  for (uint8_t c = 0; c < in_ch; c++) {
    size_t bytes = is16 ? in_entries * 2u : in_entries;
    if (pos + bytes > tag_size) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    gcol_icc_curve_t * cv = &out->in_curves[c];
    gcol_icc_curve_clear(cv, alloc);
    cv->kind = GCOL_ICC_CURVE_SAMPLED;
    cv->sample_count = in_entries;
    cv->samples = (uint16_t *)gcol_alloc(alloc, in_entries, sizeof(uint16_t));
    if (!cv->samples) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_OOM;
    }
    for (size_t i = 0; i < in_entries; i++) {
      if (is16) {
        cv->samples[i] = gcol_icc_read_u16(tag + pos + i * 2u);
      } else {
        cv->samples[i] = (uint16_t)((uint16_t)tag[pos + i] * 257u);
      }
    }
    pos += bytes;
  }

  size_t nclut = clut_len(grid, in_ch, out_ch);
  if (nclut == 0u) {
    gcol_icc_lut_clear(out, alloc);
    return GCOL_ERR_LIMIT;
  }
  size_t clut_bytes = is16 ? nclut * 2u : nclut;
  if (pos + clut_bytes > tag_size) {
    gcol_icc_lut_clear(out, alloc);
    return GCOL_ERR_CORRUPT;
  }
  out->clut = (double *)gcol_alloc(alloc, nclut, sizeof(double));
  if (!out->clut) {
    gcol_icc_lut_clear(out, alloc);
    return GCOL_ERR_OOM;
  }
  out->clut_count = nclut;
  out->clut_precision = is16 ? 2u : 1u;
  for (size_t i = 0; i < nclut; i++) {
    if (is16) {
      out->clut[i] = (double)gcol_icc_read_u16(tag + pos + i * 2u) / 65535.0;
    } else {
      out->clut[i] = (double)tag[pos + i] / 255.0;
    }
  }
  pos += clut_bytes;

  for (uint8_t c = 0; c < out_ch; c++) {
    size_t bytes = is16 ? out_entries * 2u : out_entries;
    if (pos + bytes > tag_size) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    gcol_icc_curve_t * cv = &out->out_curves[c];
    gcol_icc_curve_clear(cv, alloc);
    cv->kind = GCOL_ICC_CURVE_SAMPLED;
    cv->sample_count = out_entries;
    cv->samples = (uint16_t *)gcol_alloc(alloc, out_entries, sizeof(uint16_t));
    if (!cv->samples) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_OOM;
    }
    for (size_t i = 0; i < out_entries; i++) {
      if (is16) {
        cv->samples[i] = gcol_icc_read_u16(tag + pos + i * 2u);
      } else {
        cv->samples[i] = (uint16_t)((uint16_t)tag[pos + i] * 257u);
      }
    }
    pos += bytes;
  }

  return GCOL_OK;
}

static GCOL_Result read_curve_set(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * tag, size_t tag_size,
    uint32_t offset, uint8_t n, gcol_icc_curve_t ** out) {
  if (offset == 0u) {
    return alloc_identity_curves(alloc, n, out);
  }
  if ((size_t)offset >= tag_size) {
    return GCOL_ERR_CORRUPT;
  }
  gcol_icc_curve_t * c =
      (gcol_icc_curve_t *)gcol_alloc(alloc, n, sizeof(gcol_icc_curve_t));
  if (!c) {
    return GCOL_ERR_OOM;
  }
  size_t pos = offset;
  for (uint8_t i = 0; i < n; i++) {
    size_t consumed = 0;
    GCOL_Result r = read_curv_bytes(
        alloc, lim, tag + pos, tag_size - pos, &c[i], &consumed);
    if (r != GCOL_OK) {
      for (uint8_t j = 0; j < i; j++) {
        gcol_icc_curve_clear(&c[j], alloc);
      }
      alloc->free_fn(alloc->ctx, c);
      return r;
    }
    pos += consumed;
  }
  *out = c;
  return GCOL_OK;
}

static GCOL_Result parse_mab(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * tag, size_t tag_size,
    bool pcs_lab, gcol_icc_lut_t * out) {
  /* lutAtoBType / lutBtoAType share this layout. B→A is accepted so fixtures
   * read; transforms into PCS use A→B tags selected by the parser. */
  if (tag_size < 32u) {
    return GCOL_ERR_CORRUPT;
  }
  uint8_t in_ch = tag[8];
  uint8_t out_ch = tag[9];
  if (in_ch < 1u || in_ch > 4u || out_ch < 1u || out_ch > 4u) {
    return GCOL_ERR_UNSUPPORTED;
  }
  uint32_t off_b = gcol_icc_read_u32(tag + 12);
  uint32_t off_mat = gcol_icc_read_u32(tag + 16);
  uint32_t off_m = gcol_icc_read_u32(tag + 20);
  uint32_t off_clut = gcol_icc_read_u32(tag + 24);
  uint32_t off_a = gcol_icc_read_u32(tag + 28);

  gcol_icc_lut_clear(out, alloc);
  out->input_chan = in_ch;
  out->output_chan = out_ch;
  out->pcs_lab = pcs_lab;
  out->e[0] = out->e[4] = out->e[8] = 1.0;

  GCOL_Result r =
      read_curve_set(alloc, lim, tag, tag_size, off_a, in_ch, &out->in_curves);
  if (r != GCOL_OK) {
    gcol_icc_lut_clear(out, alloc);
    return r;
  }
  r = read_curve_set(
      alloc, lim, tag, tag_size, off_m, out_ch, &out->m_curves);
  if (r != GCOL_OK) {
    gcol_icc_lut_clear(out, alloc);
    return r;
  }
  r = read_curve_set(
      alloc, lim, tag, tag_size, off_b, out_ch, &out->out_curves);
  if (r != GCOL_OK) {
    gcol_icc_lut_clear(out, alloc);
    return r;
  }

  if (off_mat != 0u) {
    if ((size_t)off_mat + 12u * 4u > tag_size) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    /* 3x3 matrix + 3 offsets; ignore offsets (absolute intent). */
    for (int i = 0; i < 9; i++) {
      out->e[i] = gcol_icc_read_s15f16(tag + off_mat + (size_t)i * 4u);
    }
    out->has_matrix = true;
  }

  if (off_clut != 0u) {
    if ((size_t)off_clut + 20u > tag_size) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    const unsigned char * c = tag + off_clut;
    uint8_t grid = c[0];
    /* Bytes 1..15: grid points per dimension (we require equal). */
    for (uint8_t i = 1; i < in_ch; i++) {
      if (c[i] != grid) {
        gcol_icc_lut_clear(out, alloc);
        return GCOL_ERR_UNSUPPORTED;
      }
    }
    uint8_t precision = c[16];
    if (grid < 2u || (precision != 1u && precision != 2u)) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    if ((size_t)grid > lim->lut_grid) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_LIMIT;
    }
    out->grid = grid;
    size_t nclut = clut_len(grid, in_ch, out_ch);
    if (nclut == 0u) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_LIMIT;
    }
    size_t data_off = off_clut + 20u;
    size_t clut_bytes = (precision == 1u) ? nclut : nclut * 2u;
    if (data_off + clut_bytes > tag_size) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_CORRUPT;
    }
    out->clut = (double *)gcol_alloc(alloc, nclut, sizeof(double));
    if (!out->clut) {
      gcol_icc_lut_clear(out, alloc);
      return GCOL_ERR_OOM;
    }
    out->clut_count = nclut;
    out->clut_precision = precision;
    for (size_t i = 0; i < nclut; i++) {
      if (precision == 1u) {
        out->clut[i] = (double)tag[data_off + i] / 255.0;
      } else {
        out->clut[i] =
            (double)gcol_icc_read_u16(tag + data_off + i * 2u) / 65535.0;
      }
    }
  }

  /* Must have something that produces PCS: B curves alone, or CLUT. */
  if (out->grid == 0u && off_b == 0u) {
    gcol_icc_lut_clear(out, alloc);
    return GCOL_ERR_UNSUPPORTED;
  }
  return GCOL_OK;
}

GCOL_Result gcol_icc_lut_parse(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * tag, size_t tag_size,
    bool pcs_lab, gcol_icc_lut_t * out) {
  if (!alloc || !lim || !tag || !out) {
    return GCOL_ERR_INVALID;
  }
  if (tag_size < 8u) {
    return GCOL_ERR_CORRUPT;
  }
  uint32_t sig = gcol_icc_read_u32(tag);
  if (sig == GCOL_ICC_SIG_MFT1) {
    return parse_mft(alloc, lim, tag, tag_size, 0, pcs_lab, out);
  }
  if (sig == GCOL_ICC_SIG_MFT2) {
    return parse_mft(alloc, lim, tag, tag_size, 1, pcs_lab, out);
  }
  if (sig == GCOL_ICC_SIG_MAB || sig == GCOL_ICC_SIG_MBA) {
    return parse_mab(alloc, lim, tag, tag_size, pcs_lab, out);
  }
  return GCOL_ERR_UNSUPPORTED;
}

/** Multilinear CLUT interpolation. */
static void clut_sample(const gcol_icc_lut_t * lut, const double * in,
    double * out) {
  const uint8_t n = lut->input_chan;
  const uint8_t m = lut->output_chan;
  const uint8_t g = lut->grid;
  double coord[4];
  size_t idx0[4];
  size_t idx1[4];
  double t[4];
  for (uint8_t i = 0; i < n; i++) {
    coord[i] = clamp01(in[i]) * (double)(g - 1u);
    idx0[i] = (size_t)coord[i];
    if (idx0[i] >= (size_t)(g - 1u)) {
      idx0[i] = (size_t)(g - 2u);
    }
    idx1[i] = idx0[i] + 1u;
    t[i] = coord[i] - (double)idx0[i];
  }

  const size_t corners = (size_t)1u << n;
  for (uint8_t o = 0; o < m; o++) {
    out[o] = 0.0;
  }
  for (size_t mask = 0; mask < corners; mask++) {
    size_t flat = 0;
    double w = 1.0;
    for (uint8_t i = 0; i < n; i++) {
      size_t ix = (mask & ((size_t)1u << i)) ? idx1[i] : idx0[i];
      flat = flat * (size_t)g + ix;
      w *= (mask & ((size_t)1u << i)) ? t[i] : (1.0 - t[i]);
    }
    for (uint8_t o = 0; o < m; o++) {
      out[o] += w * lut->clut[flat * (size_t)m + (size_t)o];
    }
  }
}

static void lab_encoded_to_xyz(const double lab_enc[3], double xyz[3]) {
  /* ICC PCS Lab: L_enc in [0,1] → L* = L_enc * 100; a/b_enc → a* = a*255-128
   * when stored as 8-bit; for 16-bit unit-interval CLUT output the common
   * encoding is L = enc*100, a = enc*255-128, b = enc*255-128. */
  const double L = lab_enc[0] * 100.0;
  const double a = lab_enc[1] * 255.0 - 128.0;
  const double b = lab_enc[2] * 255.0 - 128.0;
  const double fy = (L + 16.0) / 116.0;
  const double fx = fy + a / 500.0;
  const double fz = fy - b / 200.0;
  const double eps = 216.0 / 24389.0;
  const double kappa = 24389.0 / 27.0;
  double xr = (fx * fx * fx > eps) ? fx * fx * fx : (116.0 * fx - 16.0) / kappa;
  double yr = (L > kappa * eps) ? fy * fy * fy : L / kappa;
  double zr = (fz * fz * fz > eps) ? fz * fz * fz : (116.0 * fz - 16.0) / kappa;
  /* D50 white. */
  xyz[0] = xr * 0.9642;
  xyz[1] = yr * 1.0000;
  xyz[2] = zr * 0.8249;
}

GCOL_Result gcol_icc_lut_apply(const gcol_icc_lut_t * lut,
    const double * device, double xyz[3]) {
  if (!lut || !device || !xyz) {
    return GCOL_ERR_INVALID;
  }
  double stage[4];
  for (uint8_t i = 0; i < lut->input_chan; i++) {
    double v = clamp01(device[i]);
    if (lut->in_curves) {
      GCOL_Result r = gcol_icc_curve_decode(&lut->in_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
    }
    stage[i] = clamp01(v);
  }

  double mid[3] = {0, 0, 0};
  if (lut->grid >= 2u && lut->clut) {
    clut_sample(lut, stage, mid);
  } else {
    /* B-curve-only path: treat first three device channels as PCS. */
    for (uint8_t i = 0; i < lut->output_chan && i < 3u; i++) {
      mid[i] = (i < lut->input_chan) ? stage[i] : 0.0;
    }
  }

  if (lut->m_curves) {
    for (uint8_t i = 0; i < lut->output_chan && i < 3u; i++) {
      double v = mid[i];
      GCOL_Result r = gcol_icc_curve_decode(&lut->m_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
      mid[i] = v;
    }
  }

  if (lut->has_matrix && lut->output_chan == 3u) {
    double t[3];
    t[0] = lut->e[0] * mid[0] + lut->e[1] * mid[1] + lut->e[2] * mid[2];
    t[1] = lut->e[3] * mid[0] + lut->e[4] * mid[1] + lut->e[5] * mid[2];
    t[2] = lut->e[6] * mid[0] + lut->e[7] * mid[1] + lut->e[8] * mid[2];
    mid[0] = t[0];
    mid[1] = t[1];
    mid[2] = t[2];
  }

  if (lut->out_curves) {
    for (uint8_t i = 0; i < lut->output_chan && i < 3u; i++) {
      double v = clamp01(mid[i]);
      GCOL_Result r = gcol_icc_curve_decode(&lut->out_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
      mid[i] = v;
    }
  }

  if (lut->pcs_lab && lut->output_chan == 3u) {
    lab_encoded_to_xyz(mid, xyz);
  } else if (lut->grid >= 2u && lut->clut_precision != 0u) {
    /* ICC XYZ PCS in a CLUT: 8-bit is code/128, 16-bit is u1Fixed15
     * (code/32768). Storage was normalised to [0,1], so undo that scale. */
    const double scale = (lut->clut_precision == 1u) ? (255.0 / 128.0)
                                                     : (65535.0 / 32768.0);
    xyz[0] = mid[0] * scale;
    xyz[1] = mid[1] * scale;
    xyz[2] = mid[2] * scale;
  } else {
    /* B-curve-only (no CLUT): curves already emit PCS values. */
    xyz[0] = mid[0];
    xyz[1] = mid[1];
    xyz[2] = mid[2];
  }
  if (lut->output_chan >= 1u && lut->output_chan < 3u) {
    for (uint8_t i = lut->output_chan; i < 3u; i++) {
      xyz[i] = 0.0;
    }
  }
  return GCOL_OK;
}

GCOL_Result gcol_icc_lut_apply_b2a(const gcol_icc_lut_t * lut,
    const double xyz[3], double * device) {
  if (!lut || !xyz || !device) {
    return GCOL_ERR_INVALID;
  }
  if (lut->input_chan < 3u) {
    return GCOL_ERR_UNSUPPORTED;
  }

  /* Absolute XYZ → ICC PCS code in [0,1]. */
  double stage[4];
  if (lut->pcs_lab) {
    return GCOL_ERR_UNSUPPORTED; /* Lab PCS B2A deferred. */
  }
  const double scale = (lut->clut_precision == 1u) ? (255.0 / 128.0)
                                                   : (65535.0 / 32768.0);
  /* When B2A has no CLUT, curves already speak PCS values. */
  if (lut->grid >= 2u && lut->clut_precision != 0u) {
    for (int i = 0; i < 3; i++) {
      stage[i] = clamp01(xyz[i] / scale);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      stage[i] = clamp01(xyz[i]);
    }
  }
  for (uint8_t i = 3; i < lut->input_chan; i++) {
    stage[i] = 0.0;
  }

  for (uint8_t i = 0; i < lut->input_chan; i++) {
    double v = stage[i];
    if (lut->in_curves) {
      GCOL_Result r = gcol_icc_curve_decode(&lut->in_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
    }
    stage[i] = clamp01(v);
  }

  if (lut->has_matrix && lut->input_chan >= 3u) {
    double t[3];
    t[0] = lut->e[0] * stage[0] + lut->e[1] * stage[1] + lut->e[2] * stage[2];
    t[1] = lut->e[3] * stage[0] + lut->e[4] * stage[1] + lut->e[5] * stage[2];
    t[2] = lut->e[6] * stage[0] + lut->e[7] * stage[1] + lut->e[8] * stage[2];
    stage[0] = t[0];
    stage[1] = t[1];
    stage[2] = t[2];
  }

  double mid[4] = {0, 0, 0, 0};
  if (lut->grid >= 2u && lut->clut) {
    clut_sample(lut, stage, mid);
  } else {
    for (uint8_t i = 0; i < lut->output_chan && i < 4u; i++) {
      mid[i] = (i < lut->input_chan) ? stage[i] : 0.0;
    }
  }

  if (lut->m_curves) {
    for (uint8_t i = 0; i < lut->output_chan && i < 4u; i++) {
      double v = mid[i];
      GCOL_Result r = gcol_icc_curve_decode(&lut->m_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
      mid[i] = v;
    }
  }

  for (uint8_t i = 0; i < lut->output_chan; i++) {
    double v = clamp01(mid[i]);
    if (lut->out_curves) {
      GCOL_Result r = gcol_icc_curve_decode(&lut->out_curves[i], v, &v);
      if (r != GCOL_OK) {
        return r;
      }
    }
    device[i] = clamp01(v);
  }
  return GCOL_OK;
}
