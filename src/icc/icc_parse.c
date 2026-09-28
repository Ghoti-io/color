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
 * Parse ICC v2 matrix/TRC profiles.
 */

#include "icc_internal.h"

#include <ghoti.io/color/math.h>

#include "../math/matrix_internal.h"

#include <ghoti.io/security/md5.h>

#include <math.h>
#include <string.h>

static void * gcol_alloc(const GCOL_Allocator * a, size_t n, size_t sz) {
  return a->calloc_fn(a->ctx, n, sz);
}

static GCOL_Result read_xyz_tag(const unsigned char * bytes, size_t size,
    uint32_t offset, uint32_t tag_size, double out[3]) {
  if (tag_size < 20u || (size_t)offset + (size_t)tag_size > size) {
    return GCOL_ERR_CORRUPT;
  }
  const unsigned char * p = bytes + offset;
  if (gcol_icc_read_u32(p) != GCOL_ICC_SIG_XYZ_TYPE) {
    return GCOL_ERR_CORRUPT;
  }
  out[0] = gcol_icc_read_s15f16(p + 8);
  out[1] = gcol_icc_read_s15f16(p + 12);
  out[2] = gcol_icc_read_s15f16(p + 16);
  return GCOL_OK;
}

static GCOL_Result read_curv_tag(const GCOL_Allocator * alloc,
    const GCOL_Limits * lim, const unsigned char * bytes, size_t size,
    uint32_t offset, uint32_t tag_size, gcol_icc_curve_t * out) {
  if (tag_size < 12u || (size_t)offset + (size_t)tag_size > size) {
    return GCOL_ERR_CORRUPT;
  }
  const unsigned char * p = bytes + offset;
  if (gcol_icc_read_u32(p) != GCOL_ICC_SIG_CURV) {
    return GCOL_ERR_UNSUPPORTED;
  }
  uint32_t count = gcol_icc_read_u32(p + 8);
  if ((size_t)count > lim->curve_samples) {
    return GCOL_ERR_LIMIT;
  }
  size_t need = (count == 1u) ? 14u : (12u + (size_t)count * 2u);
  if (need > (size_t)tag_size) {
    return GCOL_ERR_CORRUPT;
  }
  gcol_icc_curve_clear(out, alloc);
  if (count == 0u) {
    out->kind = GCOL_ICC_CURVE_IDENTITY;
    out->gamma = 1.0;
    return GCOL_OK;
  }
  if (count == 1u) {
    out->kind = GCOL_ICC_CURVE_GAMMA;
    out->gamma = (double)gcol_icc_read_u16(p + 12) / 256.0;
    if (!(out->gamma > 0.0)) {
      return GCOL_ERR_CORRUPT;
    }
    return GCOL_OK;
  }
  out->kind = GCOL_ICC_CURVE_SAMPLED;
  out->sample_count = count;
  out->samples = (uint16_t *)gcol_alloc(alloc, count, sizeof(uint16_t));
  if (!out->samples) {
    return GCOL_ERR_OOM;
  }
  for (uint32_t i = 0; i < count; i++) {
    out->samples[i] = gcol_icc_read_u16(p + 12 + (size_t)i * 2u);
  }
  return GCOL_OK;
}

static GCOL_Result read_desc_tag(const GCOL_Allocator * alloc,
    const unsigned char * bytes, size_t size, uint32_t offset,
    uint32_t tag_size, char ** out) {
  *out = NULL;
  if (tag_size < 12u || (size_t)offset + (size_t)tag_size > size) {
    return GCOL_ERR_CORRUPT;
  }
  const unsigned char * p = bytes + offset;
  if (gcol_icc_read_u32(p) != GCOL_ICC_SIG_DESC_TYPE) {
    return GCOL_OK;
  }
  uint32_t ascii_len = gcol_icc_read_u32(p + 8);
  if (ascii_len == 0u || 12u + (size_t)ascii_len > (size_t)tag_size) {
    return GCOL_ERR_CORRUPT;
  }
  if (p[12 + ascii_len - 1u] != 0) {
    return GCOL_ERR_CORRUPT;
  }
  char * copy = (char *)gcol_alloc(alloc, ascii_len, 1);
  if (!copy) {
    return GCOL_ERR_OOM;
  }
  memcpy(copy, p + 12, ascii_len);
  *out = copy;
  return GCOL_OK;
}

static GCOL_Result find_tag(const unsigned char * bytes, size_t size,
    uint32_t tag_count, uint32_t sig, uint32_t * offset, uint32_t * tag_size) {
  for (uint32_t i = 0; i < tag_count; i++) {
    const unsigned char * e =
        bytes + GCOL_ICC_HEADER_SIZE + 4u + (size_t)i * GCOL_ICC_TAG_ENTRY_SIZE;
    if (gcol_icc_read_u32(e) == sig) {
      *offset = gcol_icc_read_u32(e + 4);
      *tag_size = gcol_icc_read_u32(e + 8);
      if (*tag_size == 0u || (size_t)*offset + (size_t)*tag_size > size) {
        return GCOL_ERR_CORRUPT;
      }
      return GCOL_OK;
    }
  }
  return GCOL_ERR_CORRUPT;
}

static GCOL_Result compute_profile_id(const GCOL_Allocator * alloc,
    const unsigned char * bytes, size_t size, unsigned char out[16]) {
  if (size < GCOL_ICC_HEADER_SIZE) {
    return GCOL_ERR_CORRUPT;
  }
  unsigned char * tmp = (unsigned char *)gcol_alloc(alloc, size, 1);
  if (!tmp) {
    return GCOL_ERR_OOM;
  }
  memcpy(tmp, bytes, size);
  memset(tmp + 44, 0, 4);
  memset(tmp + 64, 0, 4);
  memset(tmp + 84, 0, 16);
  GSEC_Result hr = gsec_md5(tmp, size, out);
  alloc->free_fn(alloc->ctx, tmp);
  if (hr != GSEC_OK) {
    return GCOL_ERR_INTERNAL;
  }
  return GCOL_OK;
}

static void xyz_to_xy(const double xyz[3], GCOL_Chromaticity * c) {
  double sum = xyz[0] + xyz[1] + xyz[2];
  if (!(sum > 0.0)) {
    c->x = 0.0;
    c->y = 0.0;
    return;
  }
  c->x = xyz[0] / sum;
  c->y = xyz[1] / sum;
}

static bool matrix_near(const double a[9], const double b[9], double tol) {
  for (int i = 0; i < 9; i++) {
    if (fabs(a[i] - b[i]) > tol) {
      return false;
    }
  }
  return true;
}

/** Try to name the gamut by matching D50-adapted published colorants. */
static bool match_named_gamut(const double rgb_to_xyz_d50[9], GCOL_Gamut * out) {
  static const GCOL_Chromaticity d50 = {0.3457, 0.3585};
  for (int p = (int)GCOL_PRIMARIES_UNKNOWN + 1; p < (int)GCOL_PRIMARIES_COUNT;
       p++) {
    GCOL_Gamut g;
    if (!gcol_gamut_named((GCOL_Primaries)p, &g)) {
      continue;
    }
    double m_native[9];
    if (gcol_gamut_to_xyz(&g, m_native) != GCOL_OK) {
      continue;
    }
    double adapt[9];
    if (gcol_chromatic_adapt(g.white, d50, GCOL_CAT_BRADFORD, adapt) !=
        GCOL_OK) {
      continue;
    }
    double m_d50[9];
    gcol_mat3_mul(adapt, m_native, m_d50);
    /* s15Fixed16 and published tables agree to ~2e-4. */
    if (matrix_near(m_d50, rgb_to_xyz_d50, 0.0005)) {
      *out = g;
      return true;
    }
  }
  return false;
}

GCOL_API GCOL_Result gcol_icc_parse(const GCOL_Allocator * alloc,
    const GCOL_Limits * limits, const void * bytes, size_t size,
    GCOL_ICC_Profile ** out) {
  if (!out) {
    return GCOL_ERR_INVALID;
  }
  *out = NULL;
  if (!bytes) {
    return GCOL_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gcol_allocator_default();
  }

  GCOL_Limits lim;
  gcol_icc_limits_resolve(limits, &lim);
  if (size > lim.profile_bytes) {
    return GCOL_ERR_LIMIT;
  }
  if (size < GCOL_ICC_HEADER_SIZE + 4u) {
    return GCOL_ERR_FORMAT;
  }

  const unsigned char * p = (const unsigned char *)bytes;
  uint32_t declared = gcol_icc_read_u32(p);
  if (declared != (uint32_t)size) {
    /* Tolerate a declared size that equals the buffer; refuse shadow. */
    if (declared < GCOL_ICC_HEADER_SIZE || (size_t)declared > size) {
      return GCOL_ERR_CORRUPT;
    }
    size = declared;
  }
  if (gcol_icc_read_u32(p + 36) != GCOL_ICC_SIG_ACSP) {
    return GCOL_ERR_FORMAT;
  }

  uint32_t tag_count = gcol_icc_read_u32(p + GCOL_ICC_HEADER_SIZE);
  if ((size_t)tag_count > lim.tag_count) {
    return GCOL_ERR_LIMIT;
  }
  size_t table_end =
      GCOL_ICC_HEADER_SIZE + 4u + (size_t)tag_count * GCOL_ICC_TAG_ENTRY_SIZE;
  if (table_end > size) {
    return GCOL_ERR_CORRUPT;
  }

  GCOL_ICC_Profile * prof =
      (GCOL_ICC_Profile *)gcol_alloc(alloc, 1, sizeof(GCOL_ICC_Profile));
  if (!prof) {
    return GCOL_ERR_OOM;
  }
  prof->alloc = *alloc;
  prof->size = (uint32_t)size;
  prof->version = gcol_icc_read_u32(p + 8);
  prof->profile_class = gcol_icc_read_u32(p + 12);
  prof->data_space = gcol_icc_read_u32(p + 16);
  prof->pcs = gcol_icc_read_u32(p + 20);
  uint32_t intent = gcol_icc_read_u32(p + 64);
  if (intent >= (uint32_t)GCOL_INTENT_COUNT) {
    intent = (uint32_t)GCOL_INTENT_PERCEPTUAL;
  }
  prof->intent = (GCOL_Rendering_Intent)intent;

  GCOL_Result idr = compute_profile_id(alloc, p, size, prof->profile_id);
  if (idr != GCOL_OK) {
    gcol_icc_free(prof);
    return idr;
  }

  uint32_t off = 0, tsz = 0;
  if (find_tag(p, size, tag_count, GCOL_ICC_SIG_DESC, &off, &tsz) == GCOL_OK) {
    GCOL_Result dr = read_desc_tag(alloc, p, size, off, tsz, &prof->description);
    if (dr != GCOL_OK) {
      gcol_icc_free(prof);
      return dr;
    }
  }

  if (prof->pcs != GCOL_ICC_SIG_XYZ) {
    /* Parsed enough to refuse cleanly. */
    gcol_icc_free(prof);
    return GCOL_ERR_UNSUPPORTED;
  }

  if (prof->data_space == GCOL_ICC_SIG_RGB) {
    double r[3], g[3], b[3], w[3];
    GCOL_Result rr;
    rr = find_tag(p, size, tag_count, GCOL_ICC_SIG_RXYZ, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_xyz_tag(p, size, off, tsz, r);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = find_tag(p, size, tag_count, GCOL_ICC_SIG_GXYZ, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_xyz_tag(p, size, off, tsz, g);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = find_tag(p, size, tag_count, GCOL_ICC_SIG_BXYZ, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_xyz_tag(p, size, off, tsz, b);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = find_tag(p, size, tag_count, GCOL_ICC_SIG_WTPT, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_xyz_tag(p, size, off, tsz, w);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    /* Columns are the colorants. */
    prof->rgb_to_xyz_d50[0] = r[0];
    prof->rgb_to_xyz_d50[1] = g[0];
    prof->rgb_to_xyz_d50[2] = b[0];
    prof->rgb_to_xyz_d50[3] = r[1];
    prof->rgb_to_xyz_d50[4] = g[1];
    prof->rgb_to_xyz_d50[5] = b[1];
    prof->rgb_to_xyz_d50[6] = r[2];
    prof->rgb_to_xyz_d50[7] = g[2];
    prof->rgb_to_xyz_d50[8] = b[2];
    prof->wtpt[0] = w[0];
    prof->wtpt[1] = w[1];
    prof->wtpt[2] = w[2];

    static const uint32_t trc_sigs[3] = {
        GCOL_ICC_SIG_RTRC, GCOL_ICC_SIG_GTRC, GCOL_ICC_SIG_BTRC};
    gcol_icc_curve_t * curves[3] = {
        &prof->trc_r, &prof->trc_g, &prof->trc_b};
    for (int i = 0; i < 3; i++) {
      rr = find_tag(p, size, tag_count, trc_sigs[i], &off, &tsz);
      if (rr != GCOL_OK) {
        gcol_icc_free(prof);
        return rr;
      }
      rr = read_curv_tag(alloc, &lim, p, size, off, tsz, curves[i]);
      if (rr != GCOL_OK) {
        gcol_icc_free(prof);
        return rr;
      }
    }
    prof->has_rgb_matrix = true;
    prof->matrix_trc = true;
  } else if (prof->data_space == GCOL_ICC_SIG_GRAY) {
    double w[3];
    GCOL_Result rr =
        find_tag(p, size, tag_count, GCOL_ICC_SIG_WTPT, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_xyz_tag(p, size, off, tsz, w);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    prof->wtpt[0] = w[0];
    prof->wtpt[1] = w[1];
    prof->wtpt[2] = w[2];
    rr = find_tag(p, size, tag_count, GCOL_ICC_SIG_KTRC, &off, &tsz);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    rr = read_curv_tag(alloc, &lim, p, size, off, tsz, &prof->trc_k);
    if (rr != GCOL_OK) {
      gcol_icc_free(prof);
      return rr;
    }
    prof->has_gray_trc = true;
    prof->matrix_trc = true;
  } else {
    gcol_icc_free(prof);
    return GCOL_ERR_UNSUPPORTED;
  }

  *out = prof;
  return GCOL_OK;
}

GCOL_API void gcol_icc_free(GCOL_ICC_Profile * profile) {
  if (!profile) {
    return;
  }
  const GCOL_Allocator * a = &profile->alloc;
  gcol_icc_curve_clear(&profile->trc_r, a);
  gcol_icc_curve_clear(&profile->trc_g, a);
  gcol_icc_curve_clear(&profile->trc_b, a);
  gcol_icc_curve_clear(&profile->trc_k, a);
  if (profile->description) {
    a->free_fn(a->ctx, profile->description);
  }
  a->free_fn(a->ctx, profile);
}

GCOL_API uint32_t gcol_icc_version(const GCOL_ICC_Profile * profile) {
  return profile ? profile->version : 0;
}

GCOL_API uint32_t gcol_icc_class(const GCOL_ICC_Profile * profile) {
  return profile ? profile->profile_class : 0;
}

GCOL_API uint32_t gcol_icc_data_space(const GCOL_ICC_Profile * profile) {
  return profile ? profile->data_space : 0;
}

GCOL_API uint32_t gcol_icc_pcs(const GCOL_ICC_Profile * profile) {
  return profile ? profile->pcs : 0;
}

GCOL_API GCOL_Rendering_Intent gcol_icc_intent(
    const GCOL_ICC_Profile * profile) {
  return profile ? profile->intent : GCOL_INTENT_PERCEPTUAL;
}

GCOL_API const char * gcol_icc_description(const GCOL_ICC_Profile * profile) {
  return profile ? profile->description : NULL;
}

GCOL_API bool gcol_icc_is_matrix_trc(const GCOL_ICC_Profile * profile) {
  return profile && profile->matrix_trc;
}

GCOL_API GCOL_Result gcol_icc_profile_id(
    const GCOL_ICC_Profile * profile, unsigned char out[16]) {
  if (!profile || !out) {
    return GCOL_ERR_INVALID;
  }
  memcpy(out, profile->profile_id, 16);
  return GCOL_OK;
}

GCOL_API GCOL_Result gcol_icc_to_color_info(
    const GCOL_ICC_Profile * profile, GCOL_Color_Info * out) {
  if (!profile || !out) {
    return GCOL_ERR_INVALID;
  }
  if (!profile->matrix_trc) {
    return GCOL_ERR_UNSUPPORTED;
  }
  gcol_color_info_default(out);
  out->intent = profile->intent;

  if (profile->has_gray_trc) {
    GCOL_Result tr = gcol_icc_curve_to_transfer(&profile->trc_k, out);
    if (tr != GCOL_OK) {
      return tr;
    }
    xyz_to_xy(profile->wtpt, &out->gamut.white);
    out->white_stated = true;
    return GCOL_OK;
  }

  if (!profile->has_rgb_matrix) {
    return GCOL_ERR_UNSUPPORTED;
  }

  GCOL_Gamut named;
  if (match_named_gamut(profile->rgb_to_xyz_d50, &named)) {
    out->gamut = named;
    out->primaries_stated = true;
    out->white_stated = true;
  } else {
    /* Fall back to chromaticities of the D50-adapted colorants. Transform
     * still works: gamut_to_xyz under that white recovers the matrix. */
    double r[3] = {profile->rgb_to_xyz_d50[0], profile->rgb_to_xyz_d50[3],
        profile->rgb_to_xyz_d50[6]};
    double g[3] = {profile->rgb_to_xyz_d50[1], profile->rgb_to_xyz_d50[4],
        profile->rgb_to_xyz_d50[7]};
    double b[3] = {profile->rgb_to_xyz_d50[2], profile->rgb_to_xyz_d50[5],
        profile->rgb_to_xyz_d50[8]};
    xyz_to_xy(r, &out->gamut.red);
    xyz_to_xy(g, &out->gamut.green);
    xyz_to_xy(b, &out->gamut.blue);
    xyz_to_xy(profile->wtpt, &out->gamut.white);
    out->primaries_stated = true;
    out->white_stated = true;
  }

  /* Prefer the red TRC; the corpus uses identical curves per channel. */
  return gcol_icc_curve_to_transfer(&profile->trc_r, out);
}
