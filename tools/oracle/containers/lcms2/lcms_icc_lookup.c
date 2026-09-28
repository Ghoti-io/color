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
 * Device → PCS XYZ through littleCMS 2, printed like gcol_icc_lookup so
 * verify_lcms.py can compare the two. Built into the pinned oracle image;
 * not shipped with the library.
 *
 * Usage:
 *   lcms_icc_lookup <profile.icc> <v0> [v1 [v2 [v3]]]
 *
 * Device channels are in [0,1]. CMYK is scaled to littleCMS's 0..100 percent
 * ink internally. Prints one line: X Y Z.
 */

#include <stdio.h>
#include <stdlib.h>

#include <lcms2.h>

int main(int argc, char ** argv) {
  if (argc < 3 || argc > 6) {
    fprintf(stderr,
        "usage: lcms_icc_lookup <profile.icc> <v0> [v1 [v2 [v3]]]\n");
    return 2;
  }

  cmsHPROFILE in = cmsOpenProfileFromFile(argv[1], "r");
  if (!in) {
    fprintf(stderr, "lcms_icc_lookup: cannot open %s\n", argv[1]);
    return 1;
  }

  cmsHPROFILE out = cmsCreateXYZProfile();
  if (!out) {
    fprintf(stderr, "lcms_icc_lookup: cmsCreateXYZProfile failed\n");
    cmsCloseProfile(in);
    return 1;
  }

  cmsColorSpaceSignature cs = cmsGetColorSpace(in);
  cmsUInt32Number in_fmt;
  int nchans;
  if (cs == cmsSigRgbData) {
    in_fmt = TYPE_RGB_DBL;
    nchans = 3;
  } else if (cs == cmsSigCmykData) {
    in_fmt = TYPE_CMYK_DBL;
    nchans = 4;
  } else if (cs == cmsSigGrayData) {
    in_fmt = TYPE_GRAY_DBL;
    nchans = 1;
  } else {
    fprintf(stderr, "lcms_icc_lookup: unsupported colorspace 0x%08x\n",
        (unsigned)cs);
    cmsCloseProfile(out);
    cmsCloseProfile(in);
    return 1;
  }

  if (argc - 2 < nchans) {
    fprintf(stderr, "lcms_icc_lookup: need %d device channels\n", nchans);
    cmsCloseProfile(out);
    cmsCloseProfile(in);
    return 1;
  }

  /* NOOPTIMIZE: the gate wants the profile's tables, not a resampled link. */
  cmsHTRANSFORM xf = cmsCreateTransform(in, in_fmt, out, TYPE_XYZ_DBL,
      INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
  if (!xf) {
    fprintf(stderr, "lcms_icc_lookup: cmsCreateTransform failed\n");
    cmsCloseProfile(out);
    cmsCloseProfile(in);
    return 1;
  }

  double device[4] = {0, 0, 0, 0};
  for (int i = 0; i < nchans; i++) {
    device[i] = strtod(argv[2 + i], NULL);
  }
  /* TYPE_CMYK_DBL is percent ink (0..100), not unit interval. */
  if (cs == cmsSigCmykData) {
    for (int i = 0; i < 4; i++) {
      device[i] *= 100.0;
    }
  }

  cmsCIEXYZ xyz;
  cmsDoTransform(xf, device, &xyz, 1);
  printf("%.10f %.10f %.10f\n", xyz.X, xyz.Y, xyz.Z);

  cmsDeleteTransform(xf);
  cmsCloseProfile(out);
  cmsCloseProfile(in);
  return 0;
}
