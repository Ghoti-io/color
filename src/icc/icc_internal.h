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

#ifndef GHOTI_IO_GCOL_ICC_INTERNAL_H
#define GHOTI_IO_GCOL_ICC_INTERNAL_H

#include <ghoti.io/color/macros.h>

#include <ghoti.io/color/allocator.h>
#include <ghoti.io/color/color.h>
#include <ghoti.io/color/icc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward-declared; full type in icc_lut.h. */
typedef struct gcol_icc_lut_s gcol_icc_lut_t;

#define GCOL_ICC_HEADER_SIZE 128u
#define GCOL_ICC_TAG_ENTRY_SIZE 12u

#define GCOL_ICC_SIG(a, b, c, d)                                         \
  (((uint32_t)(unsigned char)(a) << 24) | ((uint32_t)(unsigned char)(b) << 16) | \
      ((uint32_t)(unsigned char)(c) << 8) | (uint32_t)(unsigned char)(d))

#define GCOL_ICC_SIG_ACSP GCOL_ICC_SIG('a', 'c', 's', 'p')
#define GCOL_ICC_SIG_MNTR GCOL_ICC_SIG('m', 'n', 't', 'r')
#define GCOL_ICC_SIG_PRTR GCOL_ICC_SIG('p', 'r', 't', 'r')
#define GCOL_ICC_SIG_SPAC GCOL_ICC_SIG('s', 'p', 'a', 'c')
#define GCOL_ICC_SIG_RGB GCOL_ICC_SIG('R', 'G', 'B', ' ')
#define GCOL_ICC_SIG_GRAY GCOL_ICC_SIG('G', 'R', 'A', 'Y')
#define GCOL_ICC_SIG_CMYK GCOL_ICC_SIG('C', 'M', 'Y', 'K')
#define GCOL_ICC_SIG_XYZ GCOL_ICC_SIG('X', 'Y', 'Z', ' ')
#define GCOL_ICC_SIG_LAB GCOL_ICC_SIG('L', 'a', 'b', ' ')
#define GCOL_ICC_SIG_RXYZ GCOL_ICC_SIG('r', 'X', 'Y', 'Z')
#define GCOL_ICC_SIG_GXYZ GCOL_ICC_SIG('g', 'X', 'Y', 'Z')
#define GCOL_ICC_SIG_BXYZ GCOL_ICC_SIG('b', 'X', 'Y', 'Z')
#define GCOL_ICC_SIG_WTPT GCOL_ICC_SIG('w', 't', 'p', 't')
#define GCOL_ICC_SIG_RTRC GCOL_ICC_SIG('r', 'T', 'R', 'C')
#define GCOL_ICC_SIG_GTRC GCOL_ICC_SIG('g', 'T', 'R', 'C')
#define GCOL_ICC_SIG_BTRC GCOL_ICC_SIG('b', 'T', 'R', 'C')
#define GCOL_ICC_SIG_KTRC GCOL_ICC_SIG('k', 'T', 'R', 'C')
#define GCOL_ICC_SIG_DESC GCOL_ICC_SIG('d', 'e', 's', 'c')
#define GCOL_ICC_SIG_CURV GCOL_ICC_SIG('c', 'u', 'r', 'v')
#define GCOL_ICC_SIG_XYZ_TYPE GCOL_ICC_SIG('X', 'Y', 'Z', ' ')
#define GCOL_ICC_SIG_DESC_TYPE GCOL_ICC_SIG('d', 'e', 's', 'c')
#define GCOL_ICC_SIG_A2B0 GCOL_ICC_SIG('A', '2', 'B', '0')
#define GCOL_ICC_SIG_A2B1 GCOL_ICC_SIG('A', '2', 'B', '1')
#define GCOL_ICC_SIG_A2B2 GCOL_ICC_SIG('A', '2', 'B', '2')
#define GCOL_ICC_SIG_B2A0 GCOL_ICC_SIG('B', '2', 'A', '0')
#define GCOL_ICC_SIG_B2A1 GCOL_ICC_SIG('B', '2', 'A', '1')
#define GCOL_ICC_SIG_B2A2 GCOL_ICC_SIG('B', '2', 'A', '2')

/** Tone curve recovered from a curv or para tag. */
typedef struct {
  enum {
    GCOL_ICC_CURVE_IDENTITY = 0, ///< Zero entries.
    GCOL_ICC_CURVE_GAMMA,        ///< One u8Fixed8 entry.
    GCOL_ICC_CURVE_SAMPLED,      ///< N uint16 samples, device→linear.
    GCOL_ICC_CURVE_PARAMETRIC    ///< ICC parametricCurveType (function 0..4).
  } kind;
  double gamma;       ///< When GAMMA.
  uint16_t * samples; ///< When SAMPLED; length sample_count.
  size_t sample_count;
  /** parametricCurveType: function 0..4 and up to seven s15Fixed16 params. */
  uint16_t para_function;
  uint8_t para_count;
  double para[7];
} gcol_icc_curve_t;

struct GCOL_ICC_Profile {
  GCOL_Allocator alloc;
  uint32_t size;
  uint32_t version;
  uint32_t profile_class;
  uint32_t data_space;
  uint32_t pcs;
  GCOL_Rendering_Intent intent;
  char * description;
  bool matrix_trc;
  bool has_rgb_matrix;
  bool has_gray_trc;
  bool has_lut; ///< Any AToB or BToA LUT present.
  /** Row-major RGB→XYZ under D50 (columns are rXYZ, gXYZ, bXYZ). */
  double rgb_to_xyz_d50[9];
  double wtpt[3];
  gcol_icc_curve_t trc_r;
  gcol_icc_curve_t trc_g;
  gcol_icc_curve_t trc_b;
  gcol_icc_curve_t trc_k;
  /** Relative-colorimetric AToB1 (device→PCS), when present. */
  gcol_icc_lut_t * a2b;
  /** Perceptual AToB0 (device→PCS), when present. */
  gcol_icc_lut_t * a2b0;
  /** Saturation AToB2 (device→PCS), when present. */
  gcol_icc_lut_t * a2b2;
  /** Relative-colorimetric BToA1 (PCS→device), when present. */
  gcol_icc_lut_t * b2a;
  /** Perceptual BToA0 (PCS→device), when present. */
  gcol_icc_lut_t * b2a0;
  /** Saturation BToA2 (PCS→device), when present. */
  gcol_icc_lut_t * b2a2;
  /** Media white point from wtpt, when present (D50 if absent). */
  bool has_wtpt;
  unsigned char profile_id[16];
};

/** True when @p profile carries any device→PCS LUT. */
bool gcol_icc_has_lut(const GCOL_ICC_Profile * profile);

/**
 * Device→PCS LUT for @p intent.
 * Perceptual returns A2B0 only; saturation returns A2B2 only (never invent
 * a mapping). Relative prefers A2B1 and may fall back to A2B0 when that is
 * the only table (ICC-permitted). Absolute uses the colorimetric tables
 * (same selection as relative); the media-white scale is applied by the
 * transform, not here.
 */
const gcol_icc_lut_t * gcol_icc_a2b_for_intent(
    const GCOL_ICC_Profile * profile, GCOL_Rendering_Intent intent);

/** Relative-preferred device→PCS LUT (A2B1, else A2B0), or NULL. */
const gcol_icc_lut_t * gcol_icc_a2b(const GCOL_ICC_Profile * profile);

/** True when @p profile carries any PCS→device LUT. */
bool gcol_icc_has_b2a(const GCOL_ICC_Profile * profile);

/**
 * PCS→device LUT for @p intent. Same selection rule as
 * ::gcol_icc_a2b_for_intent.
 */
const gcol_icc_lut_t * gcol_icc_b2a_for_intent(
    const GCOL_ICC_Profile * profile, GCOL_Rendering_Intent intent);

/** Relative-preferred PCS→device LUT (B2A1, else B2A0), or NULL. */
const gcol_icc_lut_t * gcol_icc_b2a(const GCOL_ICC_Profile * profile);

uint32_t gcol_icc_read_u32(const unsigned char * p);
uint16_t gcol_icc_read_u16(const unsigned char * p);
int32_t gcol_icc_read_s32(const unsigned char * p);
double gcol_icc_read_s15f16(const unsigned char * p);

void gcol_icc_limits_resolve(const GCOL_Limits * in, GCOL_Limits * out);
void gcol_icc_curve_clear(gcol_icc_curve_t * c, const GCOL_Allocator * alloc);
GCOL_Result gcol_icc_curve_decode(
    const gcol_icc_curve_t * c, double encoded, double * linear);
GCOL_Result gcol_icc_curve_encode(
    const gcol_icc_curve_t * c, double linear, double * encoded);
GCOL_Result gcol_icc_curve_to_transfer(
    const gcol_icc_curve_t * c, GCOL_Color_Info * info);

#endif /* GHOTI_IO_GCOL_ICC_INTERNAL_H */
