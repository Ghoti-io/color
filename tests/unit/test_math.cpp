/**
 * @file
 *
 * Phase 1 colour maths: published matrices, round trips, Bradford→D50
 * colorants against the synthesizer's transcribed table, transfer curves.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/color/icc.h>
#include <ghoti.io/color/math.h>
#include <ghoti.io/color/transform.h>

#include <cmath>
#include <cstring>

namespace {

bool near(double a, double b, double tol) {
  return std::fabs(a - b) <= tol;
}

bool mat_near(const double a[9], const double b[9], double tol) {
  for (int i = 0; i < 9; i++) {
    if (!near(a[i], b[i], tol)) {
      return false;
    }
  }
  return true;
}

void mat_mul(const double a[9], const double b[9], double out[9]) {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      out[3 * r + c] = a[3 * r] * b[c] + a[3 * r + 1] * b[3 + c] +
          a[3 * r + 2] * b[6 + c];
    }
  }
}

} // namespace

// Matrices derived from the same CIE xy values this library ships, frozen
// here as the known-answer vector. Bruce Lindbloom's published figures use a
// slightly different D65 XYZ and disagree at ~1e-4; we match generate.py.
TEST(GamutMath, SrgbMatrixMatchesPublication) {
  GCOL_Gamut g;
  ASSERT_TRUE(gcol_gamut_named(GCOL_PRIMARIES_SRGB, &g));
  double m[9];
  ASSERT_EQ(gcol_gamut_to_xyz(&g, m), GCOL_OK);
  const double published[9] = {0.4123907992659593, 0.3575843393838780,
      0.1804807884018343, 0.2126390058715102, 0.7151686787677560,
      0.0721923153607337, 0.0193308187155918, 0.1191947797946260,
      0.9505321522496607};
  EXPECT_TRUE(mat_near(m, published, 1e-12));
}

TEST(GamutMath, AdobeRgbMatrixMatchesPublication) {
  GCOL_Gamut g;
  ASSERT_TRUE(gcol_gamut_named(GCOL_PRIMARIES_ADOBE_RGB, &g));
  double m[9];
  ASSERT_EQ(gcol_gamut_to_xyz(&g, m), GCOL_OK);
  const double published[9] = {0.5766690429101305, 0.1855582379065463,
      0.1882286462349947, 0.2973449752505360, 0.6273635662554661,
      0.0752914584939979, 0.0270313613864123, 0.0706888525358272,
      0.9913375368376386};
  EXPECT_TRUE(mat_near(m, published, 1e-12));
}

TEST(GamutMath, RoundTripIsIdentity) {
  for (int p = GCOL_PRIMARIES_UNKNOWN + 1; p < GCOL_PRIMARIES_COUNT; p++) {
    GCOL_Gamut g;
    if (!gcol_gamut_named(static_cast<GCOL_Primaries>(p), &g)) {
      continue;
    }
    double fwd[9], inv[9], id[9];
    ASSERT_EQ(gcol_gamut_to_xyz(&g, fwd), GCOL_OK) << p;
    ASSERT_EQ(gcol_xyz_to_gamut(&g, inv), GCOL_OK) << p;
    mat_mul(inv, fwd, id);
    for (int i = 0; i < 9; i++) {
      const double expect = (i % 4 == 0) ? 1.0 : 0.0;
      EXPECT_NEAR(id[i], expect, 1e-12) << "primaries " << p << " entry " << i;
    }
  }
}

TEST(GamutMath, HalfStatedGamutIsRefused) {
  GCOL_Gamut g = {};
  g.red = {0.64, 0.33};
  g.green = {0.30, 0.60};
  g.blue = {0.15, 0.06};
  // white left at {0,0}
  double m[9];
  EXPECT_EQ(gcol_gamut_to_xyz(&g, m), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_gamut_to_xyz(nullptr, m), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_gamut_to_xyz(&g, nullptr), GCOL_ERR_INVALID);
}

// The synthesizer's hand-transcribed sRGB colorants, which phase 1 makes
// derivable: gamut_to_xyz under D65, then Bradford to D50.
TEST(GamutMath, BradfordAdaptationRecoversSynthColorants) {
  GCOL_Gamut g;
  ASSERT_TRUE(gcol_gamut_named(GCOL_PRIMARIES_SRGB, &g));
  double rgb_xyz[9];
  ASSERT_EQ(gcol_gamut_to_xyz(&g, rgb_xyz), GCOL_OK);

  GCOL_Chromaticity d65 = {0.3127, 0.3290};
  // ICC D50 as (x,y): X=0.9642 Y=1 Z=0.8249 → x=X/(X+Y+Z), y=Y/(X+Y+Z)
  const double X = 0.9642, Y = 1.0, Z = 0.8249;
  GCOL_Chromaticity d50 = {X / (X + Y + Z), Y / (X + Y + Z)};
  double adapt[9];
  ASSERT_EQ(gcol_chromatic_adapt(d65, d50, GCOL_CAT_BRADFORD, adapt), GCOL_OK);

  double adapted[9];
  mat_mul(adapt, rgb_xyz, adapted);

  // Columns are rXYZ, gXYZ, bXYZ as icc_write's table stores them
  // (each colorant is X,Y,Z).
  const double r_xyz[3] = {0.43607, 0.22249, 0.01392};
  const double g_xyz[3] = {0.38515, 0.71687, 0.09708};
  const double b_xyz[3] = {0.14307, 0.06061, 0.71410};
  const double * published[3] = {r_xyz, g_xyz, b_xyz};
  for (int col = 0; col < 3; col++) {
    for (int row = 0; row < 3; row++) {
      EXPECT_NEAR(adapted[3 * row + col], published[col][row], 2e-4)
          << "row " << row << " col " << col;
    }
  }
}

TEST(TransferMath, SrgbRoundTrip) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_SRGB;
  for (double e = 0.0; e <= 1.0; e += 0.01) {
    const double L = gcol_transfer_decode(&info, e);
    const double back = gcol_transfer_encode(&info, L);
    EXPECT_NEAR(back, e, 1e-12) << "e=" << e;
  }
}

TEST(TransferMath, GammaRoundTrip) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_GAMMA;
  info.gamma_value = 2.2;
  for (double e = 0.0; e <= 1.0; e += 0.05) {
    EXPECT_NEAR(gcol_transfer_encode(&info, gcol_transfer_decode(&info, e)), e,
        1e-12);
  }
}

TEST(TransferMath, PqKnownPoint) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_PQ;
  // PQ's OETF at L=0 is c1^m2, not 0; round-trip linear through encode/decode.
  for (double L : {0.0, 0.01, 0.1, 0.5, 0.9, 1.0}) {
    EXPECT_NEAR(gcol_transfer_decode(&info, gcol_transfer_encode(&info, L)), L,
        1e-10)
        << L;
  }
  for (double e : {0.1, 0.5, 0.9, 1.0}) {
    EXPECT_NEAR(gcol_transfer_encode(&info, gcol_transfer_decode(&info, e)), e,
        1e-10)
        << e;
  }
}

TEST(TransferMath, HlgRoundTrip) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_HLG;
  for (double e = 0.0; e <= 1.0; e += 0.05) {
    EXPECT_NEAR(gcol_transfer_encode(&info, gcol_transfer_decode(&info, e)), e,
        1e-10)
        << e;
  }
}

/* ST 2084 EOTF known answers: encoded → normalised linear of 10000 nits.
 * Values computed from the published constants in transfer_curve.c. */
TEST(TransferMath, PqPublishedEotfPoints) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_PQ;
  (void)gcol_transfer_conventions(
      GCOL_TRANSFER_PQ, &info.reference, &info.sample_scale, &info.white_luminance);
  EXPECT_EQ(info.sample_scale, GCOL_SAMPLE_SCALE_ABSOLUTE);
  EXPECT_NEAR(info.white_luminance, 10000.0, 1e-9);

  /* EOTF(0) = 0; EOTF(1) = 1 (10000 nits). Code ~0.508 maps near 100 nits
   * (normalised 0.01); code ~0.751 maps near 1000 nits (0.1). */
  EXPECT_NEAR(gcol_transfer_decode(&info, 0.0), 0.0, 1e-12);
  EXPECT_NEAR(gcol_transfer_decode(&info, 1.0), 1.0, 1e-6);
  EXPECT_NEAR(gcol_transfer_decode(&info, 0.5081), 0.01, 5e-3);
  EXPECT_NEAR(gcol_transfer_decode(&info, 0.7518), 0.1, 5e-3);

  /* Absolute scale: normalised 0.1 → 1000 nits. */
  EXPECT_NEAR(gcol_linear_to_nits(&info, 0.1), 1000.0, 1e-9);
  EXPECT_NEAR(gcol_nits_to_linear(&info, 1000.0), 0.1, 1e-12);

  /* Gate arm: neighbouring code must not equal the 0.01 point. */
  EXPECT_GT(std::fabs(gcol_transfer_decode(&info, 0.5081) -
                  gcol_transfer_decode(&info, 0.5200)),
      1e-4);
}

TEST(TransferMath, HlgPublishedInverseOetf) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_HLG;
  /* BT.2100: E' = 0.5 → scene light = 1/12. */
  EXPECT_NEAR(gcol_transfer_decode(&info, 0.5), 1.0 / 12.0, 1e-10);
  EXPECT_NEAR(gcol_transfer_decode(&info, 0.0), 0.0, 1e-12);
}

TEST(HdrTransform, PqAbsoluteToRelativeSrgb) {
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_BT2020));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  src.transfer = GCOL_TRANSFER_PQ;
  (void)gcol_transfer_conventions(
      GCOL_TRANSFER_PQ, &src.reference, &src.sample_scale, &src.white_luminance);
  src.white_luminance = 1000.0; /* map 1000 nits to relative 1.0 */
  dst.transfer = GCOL_TRANSFER_LINEAR;
  dst.sample_scale = GCOL_SAMPLE_SCALE_RELATIVE;

  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);

  /* PQ encode of 1000/10000 = 0.1 linear → should land near relative white
   * after absolute→relative scale by white_luminance=1000. */
  const double enc = gcol_transfer_encode(&src, 0.1);
  float in[3] = {(float)enc, (float)enc, (float)enc};
  float out[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(gcol_transform_apply(x, &layout, in, &layout, out, 1), GCOL_OK);
  /* BT.2020 white → sRGB white; relative luminance ~1. */
  EXPECT_NEAR(out[1], 1.0, 0.05);

  gcol_transform_free(x);
}

TEST(LogLuv, RoundTripNeutral) {
  const double xyz_in[3] = {0.9642, 1.0, 0.8249}; /* D50-ish */
  uint32_t enc = 0;
  ASSERT_EQ(gcol_xyz_to_logluv32(xyz_in, &enc), GCOL_OK);
  EXPECT_NE(enc, 0u);
  double xyz_out[3] = {0, 0, 0};
  ASSERT_EQ(gcol_logluv32_to_xyz(enc, xyz_out), GCOL_OK);
  EXPECT_NEAR(xyz_out[1], xyz_in[1], 0.02);
  EXPECT_NEAR(xyz_out[0] / xyz_out[1], xyz_in[0] / xyz_in[1], 0.05);
}

TEST(LogLuv, HasSomewhereToGo) {
  /* Encode a scene luminance, decode to XYZ, then transform into sRGB linear
   * via a description that states absolute scene-referred light. */
  const double xyz[3] = {0.2, 0.25, 0.15};
  uint32_t enc = 0;
  ASSERT_EQ(gcol_xyz_to_logluv32(xyz, &enc), GCOL_OK);
  double decoded[3];
  ASSERT_EQ(gcol_logluv32_to_xyz(enc, decoded), GCOL_OK);

  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  src.transfer = GCOL_TRANSFER_LINEAR;
  src.reference = GCOL_REFERENCE_SCENE;
  src.sample_scale = GCOL_SAMPLE_SCALE_ABSOLUTE;
  src.white_luminance = 1.0; /* XYZ Y already in the desired relative units */
  dst.transfer = GCOL_TRANSFER_LINEAR;
  dst.sample_scale = GCOL_SAMPLE_SCALE_RELATIVE;

  /* XYZ → linear RGB under sRGB, then identity transform. */
  double xyz_to_rgb[9];
  ASSERT_EQ(gcol_xyz_to_gamut(&src.gamut, xyz_to_rgb), GCOL_OK);
  double rgb[3] = {
      xyz_to_rgb[0] * decoded[0] + xyz_to_rgb[1] * decoded[1] +
          xyz_to_rgb[2] * decoded[2],
      xyz_to_rgb[3] * decoded[0] + xyz_to_rgb[4] * decoded[1] +
          xyz_to_rgb[5] * decoded[2],
      xyz_to_rgb[6] * decoded[0] + xyz_to_rgb[7] * decoded[1] +
          xyz_to_rgb[8] * decoded[2],
  };
  EXPECT_GT(rgb[1], 0.0);
}

TEST(ToneMap, PqEetfMapsPeakToTarget) {
  /* Mastering peak → target peak; mid-tones stay below target. */
  EXPECT_NEAR(gcol_tone_map_pq_eetf(1000.0, 1000.0, 100.0, 0.0), 100.0, 1.0);
  EXPECT_LT(gcol_tone_map_pq_eetf(100.0, 1000.0, 100.0, 0.0), 100.0);
  EXPECT_GT(gcol_tone_map_pq_eetf(100.0, 1000.0, 100.0, 0.0), 0.0);
  /* Fits already: identity in nits. */
  EXPECT_NEAR(gcol_tone_map_pq_eetf(50.0, 100.0, 400.0, 0.0), 50.0, 1e-6);
}

TEST(HdrTransform, ToneMapFlagRollsHighlight) {
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_BT2020));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  src.transfer = GCOL_TRANSFER_PQ;
  (void)gcol_transfer_conventions(
      GCOL_TRANSFER_PQ, &src.reference, &src.sample_scale, &src.white_luminance);
  src.white_luminance = 1000.0;
  dst.transfer = GCOL_TRANSFER_LINEAR;
  dst.sample_scale = GCOL_SAMPLE_SCALE_RELATIVE;
  dst.white_luminance = 100.0;

  GCOL_Transform * hard = nullptr;
  GCOL_Transform * eetf = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &hard),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC,
                  GCOL_TRANSFORM_CLIP | GCOL_TRANSFORM_TONE_MAP, &eetf),
      GCOL_OK);

  /* 800 nits: hard scale → 0.8; EETF → near display peak (1.0). */
  const double enc = gcol_transfer_encode(&src, 800.0 / 10000.0);
  float in[3] = {(float)enc, (float)enc, (float)enc};
  float out_hard[3] = {0, 0, 0};
  float out_eetf[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(gcol_transform_apply(hard, &layout, in, &layout, out_hard, 1),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_apply(eetf, &layout, in, &layout, out_eetf, 1),
      GCOL_OK);
  EXPECT_NEAR(out_hard[1], 0.8, 0.08);
  EXPECT_GT(out_eetf[1], out_hard[1] + 0.05f);
  EXPECT_NEAR(out_eetf[1], 1.0, 0.15);

  gcol_transform_free(hard);
  gcol_transform_free(eetf);
}

TEST(TransformMatrix, AbsoluteSkipsBradford) {
  /* sRGB (D65) → ProPhoto (D50): relative adapts; absolute keeps illuminant. */
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_PROPHOTO));
  src.transfer = GCOL_TRANSFER_LINEAR;
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * rel = nullptr;
  GCOL_Transform * abs = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &rel),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_ABSOLUTE_COLORIMETRIC, 0, &abs),
      GCOL_OK);

  float white[3] = {1.f, 1.f, 1.f};
  float out_rel[3] = {0, 0, 0};
  float out_abs[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(gcol_transform_apply(rel, &layout, white, &layout, out_rel, 1),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_apply(abs, &layout, white, &layout, out_abs, 1),
      GCOL_OK);
  /* Relative maps adapted white → white. Absolute leaves a D65 cast. */
  EXPECT_NEAR(out_rel[0], 1.0, 0.02);
  EXPECT_NEAR(out_rel[1], 1.0, 0.02);
  EXPECT_NEAR(out_rel[2], 1.0, 0.02);
  EXPECT_GT(std::fabs(out_abs[0] - out_rel[0]) +
          std::fabs(out_abs[1] - out_rel[1]) +
          std::fabs(out_abs[2] - out_rel[2]),
      0.02);

  gcol_transform_free(rel);
  gcol_transform_free(abs);
}

TEST(TransformMatrix, BpcMapsSourceBlack) {
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  src.transfer = GCOL_TRANSFER_LINEAR;
  dst.transfer = GCOL_TRANSFER_LINEAR;
  src.white_luminance = 100.0;
  src.black_luminance = 10.0; /* 10% of white */
  dst.white_luminance = 100.0;
  dst.black_luminance = 0.0;

  GCOL_Transform * plain = nullptr;
  GCOL_Transform * bpc = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &plain),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC,
                  GCOL_TRANSFORM_CLIP | GCOL_TRANSFORM_BPC, &bpc),
      GCOL_OK);

  float gray[3] = {0.1f, 0.1f, 0.1f};
  float out_plain[3] = {0, 0, 0};
  float out_bpc[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(gcol_transform_apply(plain, &layout, gray, &layout, out_plain, 1),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_apply(bpc, &layout, gray, &layout, out_bpc, 1),
      GCOL_OK);
  EXPECT_NEAR(out_plain[1], 0.1, 0.01);
  EXPECT_NEAR(out_bpc[1], 0.0, 0.02);
  EXPECT_LT(out_bpc[1], out_plain[1] - 0.05f);

  gcol_transform_free(plain);
  gcol_transform_free(bpc);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
