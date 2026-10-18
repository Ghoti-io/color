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

/*
 * lcms_transform_vectors GRAY_ICC: what littleCMS 2.16 answers for the
 * transforms gcol_transform_apply is held to, one section per question,
 * relative colorimetric throughout.
 *
 *   # float_unbounded   linear-sRGB float -> sRGB float, values past 0..1
 *   # float_to_u8       the same inputs into 8 bits (clipped there)
 *   # gray_to_srgb_u8   every 8-bit code of GRAY_ICC -> sRGB 8-bit
 *   # srgb_to_gray_u8   sRGB 8-bit colours -> GRAY_ICC 8-bit
 *   # gray_to_srgb_f    GRAY_ICC float -> sRGB float, past 0..1 too
 *
 * Each line is "in... -> out...". littleCMS keeps float transforms
 * unbounded and clips only where an integer has to hold the result; that
 * convention, not any one number, is what these sections pin.
 */

#include <stdio.h>

#include <lcms2.h>

static cmsHPROFILE linear_srgb(void) {
  cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
  cmsCIExyYTRIPLE p = {{0.64, 0.33, 1.0}, {0.30, 0.60, 1.0},
      {0.15, 0.06, 1.0}};
  cmsToneCurve * lin = cmsBuildGamma(NULL, 1.0);
  cmsToneCurve * c[3] = {lin, lin, lin};
  cmsHPROFILE h = cmsCreateRGBProfile(&d65, &p, c);
  cmsFreeToneCurve(lin);
  return h;
}

int main(int argc, char ** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: lcms_transform_vectors GRAY_ICC\n");
    return 2;
  }
  cmsHPROFILE srgb = cmsCreate_sRGBProfile();
  cmsHPROFILE lin = linear_srgb();
  cmsHPROFILE gray = cmsOpenProfileFromFile(argv[1], "r");
  if (!srgb || !lin || !gray) {
    fprintf(stderr, "a profile did not open\n");
    return 1;
  }
  const int ri = INTENT_RELATIVE_COLORIMETRIC;

  static const float in[][3] = {{0.0f, 0.0f, 0.0f}, {0.0005f, 0.001f, 0.002f},
      {0.18f, 0.18f, 0.18f}, {0.5f, 0.25f, 0.125f}, {1.0f, 1.0f, 1.0f},
      {2.0f, 2.0f, 2.0f}, {179.0f, 179.0f, 179.0f}, {-0.1f, 0.2f, 0.3f},
      {1.5f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}, {-0.5f, -0.5f, -0.5f},
      {0.9f, 1.1f, 0.2f}};
  const unsigned n = sizeof(in) / sizeof(in[0]);
  float out[sizeof(in) / sizeof(in[0])][3];
  cmsUInt8Number out8[sizeof(in) / sizeof(in[0])][3];

  cmsHTRANSFORM t =
      cmsCreateTransform(lin, TYPE_RGB_FLT, srgb, TYPE_RGB_FLT, ri, 0);
  cmsDoTransform(t, in, out, n);
  cmsDeleteTransform(t);
  printf("# float_unbounded\n");
  for (unsigned i = 0; i < n; i++) {
    printf("%.9g %.9g %.9g -> %.9g %.9g %.9g\n", in[i][0], in[i][1], in[i][2],
        out[i][0], out[i][1], out[i][2]);
  }

  t = cmsCreateTransform(lin, TYPE_RGB_FLT, srgb, TYPE_RGB_8, ri, 0);
  cmsDoTransform(t, in, out8, n);
  cmsDeleteTransform(t);
  printf("# float_to_u8\n");
  for (unsigned i = 0; i < n; i++) {
    printf("%.9g %.9g %.9g -> %u %u %u\n", in[i][0], in[i][1], in[i][2],
        out8[i][0], out8[i][1], out8[i][2]);
  }

  cmsUInt8Number g[256], rgb[256][3];
  for (unsigned i = 0; i < 256u; i++) {
    g[i] = (cmsUInt8Number)i;
  }
  t = cmsCreateTransform(gray, TYPE_GRAY_8, srgb, TYPE_RGB_8, ri, 0);
  cmsDoTransform(t, g, rgb, 256);
  cmsDeleteTransform(t);
  printf("# gray_to_srgb_u8\n");
  for (unsigned i = 0; i < 256u; i++) {
    printf("%u -> %u %u %u\n", i, rgb[i][0], rgb[i][1], rgb[i][2]);
  }

  /* Neutrals, primaries and mixtures: a gray destination is luminance. */
  cmsUInt8Number col[64][3], back[64];
  unsigned m = 0;
  for (unsigned v = 0; v < 256u; v += 17u) {
    col[m][0] = col[m][1] = col[m][2] = (cmsUInt8Number)v;
    m++;
  }
  static const cmsUInt8Number extra[][3] = {{255, 0, 0}, {0, 255, 0},
      {0, 0, 255}, {255, 255, 0}, {0, 255, 255}, {255, 0, 255},
      {200, 100, 50}, {50, 100, 200}, {128, 64, 32}, {12, 240, 99}};
  for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); i++, m++) {
    col[m][0] = extra[i][0];
    col[m][1] = extra[i][1];
    col[m][2] = extra[i][2];
  }
  t = cmsCreateTransform(srgb, TYPE_RGB_8, gray, TYPE_GRAY_8, ri, 0);
  cmsDoTransform(t, col, back, m);
  cmsDeleteTransform(t);
  printf("# srgb_to_gray_u8\n");
  for (unsigned i = 0; i < m; i++) {
    printf("%u %u %u -> %u\n", col[i][0], col[i][1], col[i][2], back[i]);
  }

  static const float gf[] = {0.0f, 0.01f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f,
      3.0f, -0.2f};
  const unsigned k = sizeof(gf) / sizeof(gf[0]);
  float grgb[sizeof(gf) / sizeof(gf[0])][3];
  t = cmsCreateTransform(gray, TYPE_GRAY_FLT, srgb, TYPE_RGB_FLT, ri, 0);
  cmsDoTransform(t, gf, grgb, k);
  cmsDeleteTransform(t);
  printf("# gray_to_srgb_f\n");
  for (unsigned i = 0; i < k; i++) {
    printf("%.9g -> %.9g %.9g %.9g\n", gf[i], grgb[i][0], grgb[i][1],
        grgb[i][2]);
  }
  cmsCloseProfile(gray);
  cmsCloseProfile(lin);
  cmsCloseProfile(srgb);
  return 0;
}
