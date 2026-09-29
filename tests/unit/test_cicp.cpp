/**
 * @file
 *
 * H.273 coding-independent code points → GCOL_Color_Info.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/color/cicp.h>
#include <ghoti.io/color/color.h>

#include <cmath>

TEST(Cicp, SrgbPair) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_cicp_to_color_info(1u, 13u, &info));
  EXPECT_EQ(gcol_gamut_identify(&info.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
      GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(info.reference, GCOL_REFERENCE_DISPLAY);
  EXPECT_EQ(info.sample_scale, GCOL_SAMPLE_SCALE_RELATIVE);
}

TEST(Cicp, Bt2020Pq) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_cicp_to_color_info(9u, 16u, &info));
  EXPECT_EQ(gcol_gamut_identify(&info.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
      GCOL_PRIMARIES_BT2020);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_PQ);
  EXPECT_EQ(info.reference, GCOL_REFERENCE_DISPLAY);
  EXPECT_EQ(info.sample_scale, GCOL_SAMPLE_SCALE_ABSOLUTE);
  EXPECT_NEAR(info.white_luminance, 10000.0, 1e-9);
}

TEST(Cicp, DisplayP3AndLinear) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_cicp_to_color_info(12u, 8u, &info));
  EXPECT_EQ(gcol_gamut_identify(&info.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
      GCOL_PRIMARIES_DISPLAY_P3);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_LINEAR);
}

TEST(Cicp, Bt709ParametricTerms) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_cicp_to_color_info(1u, 1u, &info));
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_PARAMETRIC);
  EXPECT_NEAR(info.transfer_params[0], 1.0 / 0.45, 1e-12);
  EXPECT_NEAR(info.transfer_params[4], 0.081, 1e-12);
}

TEST(Cicp, TheatricalDciP3Unmapped) {
  /* Code 11 shares Display P3 primaries under a different white. */
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  EXPECT_FALSE(gcol_cicp_to_color_info(11u, 99u, &info));
  EXPECT_FALSE(info.primaries_stated);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_UNKNOWN);
}

TEST(Cicp, NullOutRefused) {
  EXPECT_FALSE(gcol_cicp_to_color_info(1u, 13u, nullptr));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
