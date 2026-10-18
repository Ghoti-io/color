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
 * logluv_vectors DIR: ask libtiff what LogLuv codes decode to and what XYZ
 * encodes to, and write the answers under DIR as text.
 *
 * Decode: every LogL16 code; every upper half of LogLuv32 at two u'v' bytes
 * and every u'v' at one luminance; every LogLuv24 u'v' index at one
 * luminance and every luminance at one index.  Encode: 4096 XYZ triples,
 * luminance spread log-uniformly over 2^-45..2^45 with zeros, negatives and
 * values past each encoding's ceiling mixed in, chromaticities over and past
 * the spectral locus.  libtiff dithers its encoder by default; NODITHER is
 * set so the answers are a function of the input.
 */
#include <tiffio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void put(const char * name, int photometric, int compression,
    int data_format, uint32_t w, uint32_t h, const void * buf, size_t bpp) {
  TIFF * t = TIFFOpen(name, "w");
  if (!t) {
    fprintf(stderr, "cannot write %s\n", name);
    exit(1);
  }
  TIFFSetField(t, TIFFTAG_IMAGEWIDTH, w);
  TIFFSetField(t, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(t, TIFFTAG_PHOTOMETRIC, photometric);
  TIFFSetField(t, TIFFTAG_COMPRESSION, compression);
  TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL,
      photometric == PHOTOMETRIC_LOGL ? 1 : 3);
  TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 1);
  TIFFSetField(t, TIFFTAG_SGILOGDATAFMT, data_format);
  TIFFSetField(t, TIFFTAG_SGILOGENCODE, SGILOGENCODE_NODITHER);
  for (uint32_t y = 0; y < h; y++) {
    if (TIFFWriteScanline(t, (char *)buf + (size_t)y * w * bpp, y, 0) < 0) {
      fprintf(stderr, "write failed: %s\n", name);
      exit(1);
    }
  }
  TIFFClose(t);
}

static void get(const char * name, int data_format, uint32_t w, uint32_t h,
    void * buf, size_t bpp) {
  TIFF * t = TIFFOpen(name, "r");
  if (!t) {
    fprintf(stderr, "cannot read %s\n", name);
    exit(1);
  }
  TIFFSetField(t, TIFFTAG_SGILOGDATAFMT, data_format);
  for (uint32_t y = 0; y < h; y++) {
    if (TIFFReadScanline(t, (char *)buf + (size_t)y * w * bpp, y, 0) < 0) {
      fprintf(stderr, "read failed: %s\n", name);
      exit(1);
    }
  }
  TIFFClose(t);
}

static FILE * out(const char * name) {
  FILE * f = fopen(name, "w");
  if (!f) {
    fprintf(stderr, "cannot write %s\n", name);
    exit(1);
  }
  return f;
}

// A fixed LCG, so the encode inputs are the same on every run.
static double next(unsigned * s) {
  *s = *s * 1103515245u + 12345u;
  return (double)((*s >> 8) & 0xffffffu) / 16777216.0;
}

int main(int argc, char ** argv) {
  if (argc != 2 || chdir(argv[1]) != 0) {
    fprintf(stderr, "usage: logluv_vectors DIR\n");
    return 2;
  }

  // LogL16: every code.
  {
    uint16_t * c = malloc(2 * 65536);
    float * f = malloc(4 * 65536);
    for (int i = 0; i < 65536; i++) {
      c[i] = (uint16_t)i;
    }
    put("l16.tif", PHOTOMETRIC_LOGL, COMPRESSION_SGILOG, SGILOGDATAFMT_16BIT,
        256, 256, c, 2);
    get("l16.tif", SGILOGDATAFMT_FLOAT, 256, 256, f, 4);
    FILE * o = out("dec_l16.txt");
    for (int i = 0; i < 65536; i++) {
      fprintf(o, "%04x %.9g\n", i, f[i]);
    }
    fclose(o);
    free(c);
    free(f);
  }

  // LogLuv32: every upper half at u'v' bytes 0x56c2 and 0x0000, and every
  // u'v' at upper half 0x3c00.
  {
    const uint32_t n = 65536u * 3u;
    uint32_t * c = malloc(4 * n);
    float * f = malloc(12 * n);
    for (uint32_t i = 0; i < 65536u; i++) {
      c[i] = (i << 16) | 0x56c2u;
      c[65536u + i] = i << 16;
      c[131072u + i] = (0x3c00u << 16) | i;
    }
    put("l32.tif", PHOTOMETRIC_LOGLUV, COMPRESSION_SGILOG, SGILOGDATAFMT_RAW,
        256, 768, c, 4);
    get("l32.tif", SGILOGDATAFMT_FLOAT, 256, 768, f, 12);
    FILE * o = out("dec_luv32.txt");
    for (uint32_t i = 0; i < n; i++) {
      fprintf(o, "%08x %.9g %.9g %.9g\n", c[i], f[3 * i], f[3 * i + 1],
          f[3 * i + 2]);
    }
    fclose(o);
    free(c);
    free(f);
  }

  // LogLuv24: every u'v' index at luminance 768, every luminance at index
  // 8000.
  {
    const uint32_t n = 16384u + 1024u;
    const uint32_t w = 256u, h = (n + 255u) / 256u;
    uint32_t * c = calloc((size_t)w * h, 4);
    float * f = calloc((size_t)w * h, 12);
    for (uint32_t i = 0; i < 16384u; i++) {
      c[i] = (768u << 14) | i;
    }
    for (uint32_t i = 0; i < 1024u; i++) {
      c[16384u + i] = (i << 14) | 8000u;
    }
    put("l24.tif", PHOTOMETRIC_LOGLUV, COMPRESSION_SGILOG24,
        SGILOGDATAFMT_RAW, w, h, c, 4);
    get("l24.tif", SGILOGDATAFMT_FLOAT, w, h, f, 12);
    FILE * o = out("dec_luv24.txt");
    for (uint32_t i = 0; i < n; i++) {
      fprintf(o, "%06x %.9g %.9g %.9g\n", c[i], f[3 * i], f[3 * i + 1],
          f[3 * i + 2]);
    }
    fclose(o);
    free(c);
    free(f);
  }

  // Encode, all three.
  {
    const uint32_t w = 256u, h = 16u, n = w * h;
    float * xyz = malloc(12 * n);
    float * y = malloc(4 * n);
    uint32_t * raw = malloc(4 * n);
    uint16_t * raw16 = malloc(2 * n);
    unsigned s = 12345u;
    for (uint32_t i = 0; i < n; i++) {
      double Y = exp2(next(&s) * 90.0 - 45.0);
      if (i % 97u == 0) {
        Y = 0.0;
      }
      if (i % 89u == 0) {
        Y = -Y;
      }
      if (i % 83u == 0) {
        Y = exp2(next(&s) * 20.0 + 60.0);
      }
      const double u = next(&s) * 0.7, v = next(&s) * 0.65;
      const double d = 6.0 * u - 16.0 * v + 12.0;
      const double cx = 9.0 * u / d, cy = 4.0 * v / d;
      xyz[3 * i] = (float)(cx / cy * Y);
      xyz[3 * i + 1] = (float)Y;
      xyz[3 * i + 2] = (float)((1.0 - cx - cy) / cy * Y);
      y[i] = (float)Y;
    }
    FILE * o;
    put("e32.tif", PHOTOMETRIC_LOGLUV, COMPRESSION_SGILOG,
        SGILOGDATAFMT_FLOAT, w, h, xyz, 12);
    get("e32.tif", SGILOGDATAFMT_RAW, w, h, raw, 4);
    o = out("enc_luv32.txt");
    for (uint32_t i = 0; i < n; i++) {
      fprintf(o, "%a %a %a %08x\n", xyz[3 * i], xyz[3 * i + 1],
          xyz[3 * i + 2], raw[i]);
    }
    fclose(o);
    put("e24.tif", PHOTOMETRIC_LOGLUV, COMPRESSION_SGILOG24,
        SGILOGDATAFMT_FLOAT, w, h, xyz, 12);
    get("e24.tif", SGILOGDATAFMT_RAW, w, h, raw, 4);
    o = out("enc_luv24.txt");
    for (uint32_t i = 0; i < n; i++) {
      fprintf(o, "%a %a %a %06x\n", xyz[3 * i], xyz[3 * i + 1],
          xyz[3 * i + 2], raw[i] & 0xffffffu);
    }
    fclose(o);
    put("e16.tif", PHOTOMETRIC_LOGL, COMPRESSION_SGILOG, SGILOGDATAFMT_FLOAT,
        w, h, y, 4);
    get("e16.tif", SGILOGDATAFMT_16BIT, w, h, raw16, 2);
    o = out("enc_l16.txt");
    for (uint32_t i = 0; i < n; i++) {
      fprintf(o, "%a %04x\n", y[i], raw16[i]);
    }
    fclose(o);
  }
  return 0;
}
