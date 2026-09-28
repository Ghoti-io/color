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
 * The library half of `make check-argyll`: device → PCS XYZ through this
 * library, printed so verify_argyll.py can compare against icclu.
 *
 * Not an example and not a test. The gate needs XYZ from our code and from
 * ArgyllCMS for the same (profile, device) pair; neither a unit test nor an
 * example can ask Argyll.
 *
 * Usage:
 *   gcol_icc_lookup <profile.icc> <v0> [v1 [v2 [v3]]]
 *
 * Prints one line: X Y Z
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ghoti.io/color/icc.h>

#include "icc_internal.h"
#include "icc_lut.h"

static int load_file(const char * path, unsigned char ** out, size_t * size) {
  FILE * f = fopen(path, "rb");
  if (!f) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }
  unsigned char * buf = (unsigned char *)malloc((size_t)n);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
    free(buf);
    fclose(f);
    return -1;
  }
  fclose(f);
  *out = buf;
  *size = (size_t)n;
  return 0;
}

static GCOL_Result matrix_device_to_xyz(const GCOL_ICC_Profile * p,
    const double * device, double xyz[3]) {
  if (!p->has_rgb_matrix) {
    return GCOL_ERR_UNSUPPORTED;
  }
  double lin[3];
  const gcol_icc_curve_t * trc[3] = {&p->trc_r, &p->trc_g, &p->trc_b};
  for (int i = 0; i < 3; i++) {
    GCOL_Result r = gcol_icc_curve_decode(trc[i], device[i], &lin[i]);
    if (r != GCOL_OK) {
      return r;
    }
  }
  xyz[0] = p->rgb_to_xyz_d50[0] * lin[0] + p->rgb_to_xyz_d50[1] * lin[1] +
      p->rgb_to_xyz_d50[2] * lin[2];
  xyz[1] = p->rgb_to_xyz_d50[3] * lin[0] + p->rgb_to_xyz_d50[4] * lin[1] +
      p->rgb_to_xyz_d50[5] * lin[2];
  xyz[2] = p->rgb_to_xyz_d50[6] * lin[0] + p->rgb_to_xyz_d50[7] * lin[1] +
      p->rgb_to_xyz_d50[8] * lin[2];
  return GCOL_OK;
}

int main(int argc, char ** argv) {
  if (argc < 3 || argc > 6) {
    fprintf(stderr,
        "usage: gcol_icc_lookup <profile.icc> <v0> [v1 [v2 [v3]]]\n");
    return 2;
  }

  unsigned char * bytes = NULL;
  size_t size = 0;
  if (load_file(argv[1], &bytes, &size) != 0) {
    fprintf(stderr, "gcol_icc_lookup: cannot read %s\n", argv[1]);
    return 1;
  }

  GCOL_ICC_Profile * profile = NULL;
  GCOL_Result pr = gcol_icc_parse(NULL, NULL, bytes, size, &profile);
  free(bytes);
  if (pr != GCOL_OK) {
    fprintf(stderr, "gcol_icc_lookup: parse failed (%s)\n",
        gcol_result_string(pr));
    return 1;
  }

  double device[4] = {0, 0, 0, 0};
  int nvals = argc - 2;
  for (int i = 0; i < nvals; i++) {
    device[i] = strtod(argv[2 + i], NULL);
  }

  double xyz[3] = {0, 0, 0};
  GCOL_Result r;
  if (gcol_icc_has_lut(profile)) {
    const gcol_icc_lut_t * lut = gcol_icc_a2b(profile);
    if (!lut || nvals < (int)lut->input_chan) {
      fprintf(stderr, "gcol_icc_lookup: need %u device channels\n",
          lut ? lut->input_chan : 0u);
      gcol_icc_free(profile);
      return 1;
    }
    r = gcol_icc_lut_apply(lut, device, xyz);
  } else if (profile->has_rgb_matrix) {
    if (nvals < 3) {
      fprintf(stderr, "gcol_icc_lookup: need 3 RGB channels\n");
      gcol_icc_free(profile);
      return 1;
    }
    r = matrix_device_to_xyz(profile, device, xyz);
  } else {
    fprintf(stderr, "gcol_icc_lookup: profile has no matrix or AToB LUT\n");
    gcol_icc_free(profile);
    return 1;
  }

  gcol_icc_free(profile);
  if (r != GCOL_OK) {
    fprintf(stderr, "gcol_icc_lookup: apply failed (%s)\n",
        gcol_result_string(r));
    return 1;
  }

  printf("%.10f %.10f %.10f\n", xyz[0], xyz[1], xyz[2]);
  return 0;
}
