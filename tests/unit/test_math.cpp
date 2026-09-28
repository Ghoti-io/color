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

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
