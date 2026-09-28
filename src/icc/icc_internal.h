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

#define GCOL_ICC_HEADER_SIZE 128u
#define GCOL_ICC_TAG_ENTRY_SIZE 12u

#define GCOL_ICC_SIG(a, b, c, d)                                         \
  (((uint32_t)(unsigned char)(a) << 24) | ((uint32_t)(unsigned char)(b) << 16) | \
      ((uint32_t)(unsigned char)(c) << 8) | (uint32_t)(unsigned char)(d))

#define GCOL_ICC_SIG_ACSP GCOL_ICC_SIG('a', 'c', 's', 'p')
#define GCOL_ICC_SIG_MNTR GCOL_ICC_SIG('m', 'n', 't', 'r')
#define GCOL_ICC_SIG_RGB GCOL_ICC_SIG('R', 'G', 'B', ' ')
#define GCOL_ICC_SIG_GRAY GCOL_ICC_SIG('G', 'R', 'A', 'Y')
#define GCOL_ICC_SIG_XYZ GCOL_ICC_SIG('X', 'Y', 'Z', ' ')
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

/** Tone curve recovered from a curv tag. */
typedef struct {
  enum {
    GCOL_ICC_CURVE_IDENTITY = 0, ///< Zero entries.
    GCOL_ICC_CURVE_GAMMA,        ///< One u8Fixed8 entry.
    GCOL_ICC_CURVE_SAMPLED       ///< N uint16 samples, device→linear.
  } kind;
  double gamma;       ///< When GAMMA.
  uint16_t * samples; ///< When SAMPLED; length sample_count.
  size_t sample_count;
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
  /** Row-major RGB→XYZ under D50 (columns are rXYZ, gXYZ, bXYZ). */
  double rgb_to_xyz_d50[9];
  double wtpt[3];
  gcol_icc_curve_t trc_r;
  gcol_icc_curve_t trc_g;
  gcol_icc_curve_t trc_b;
  gcol_icc_curve_t trc_k;
  unsigned char profile_id[16];
};

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
