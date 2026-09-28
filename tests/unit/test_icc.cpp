/**
 * @file
 *
 * Phase 2: ICC parse, recovered descriptions, swap_rg transform gate.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/color/icc.h>
#include <ghoti.io/color/transform.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> load_file(const std::string & path) {
  FILE * f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return {};
  }
  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    return {};
  }
  long n = std::ftell(f);
  if (n < 0) {
    std::fclose(f);
    return {};
  }
  if (std::fseek(f, 0, SEEK_SET) != 0) {
    std::fclose(f);
    return {};
  }
  std::vector<unsigned char> buf(static_cast<size_t>(n));
  if (n > 0 && std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
    std::fclose(f);
    return {};
  }
  std::fclose(f);
  return buf;
}

std::string icc_path(const char * name) {
  return gcoltest::data_dir() + "/icc/" + name;
}

GCOL_ICC_Profile * parse_named(const char * name) {
  auto bytes = load_file(icc_path(name));
  EXPECT_FALSE(bytes.empty()) << name;
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, nullptr, bytes.data(), bytes.size(), &p),
      GCOL_OK)
      << name;
  return p;
}

} // namespace

TEST(IccParse, AllSixCorpusProfilesParse) {
  static const char * names[] = {"srgb_g22.icc", "adobergb_g22.icc",
      "displayp3_g22.icc", "srgb_sampled_trc.icc", "gray_g22.icc",
      "swap_rg.icc"};
  for (const char * name : names) {
    GCOL_ICC_Profile * p = parse_named(name);
    ASSERT_NE(p, nullptr) << name;
    EXPECT_TRUE(gcol_icc_is_matrix_trc(p)) << name;
    EXPECT_EQ(gcol_icc_version(p), 0x02100000u) << name;
    const char * desc = gcol_icc_description(p);
    ASSERT_NE(desc, nullptr) << name;
    EXPECT_TRUE(std::strstr(desc, "Ghoti test") != nullptr) << desc;
    gcol_icc_free(p);
  }
}

TEST(IccParse, TagCountLimitIsEnforced) {
  auto bytes = load_file(icc_path("srgb_g22.icc"));
  ASSERT_FALSE(bytes.empty());
  GCOL_Limits lim;
  gcol_limits_default(&lim);
  lim.tag_count = 1;
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, &lim, bytes.data(), bytes.size(), &p),
      GCOL_ERR_LIMIT);
  EXPECT_EQ(p, nullptr);
}

TEST(IccParse, ProfileBytesLimitIsEnforced) {
  auto bytes = load_file(icc_path("srgb_sampled_trc.icc"));
  ASSERT_FALSE(bytes.empty());
  GCOL_Limits lim;
  gcol_limits_default(&lim);
  lim.profile_bytes = 100;
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, &lim, bytes.data(), bytes.size(), &p),
      GCOL_ERR_LIMIT);
}

TEST(IccParse, CurveSamplesLimitIsEnforced) {
  auto bytes = load_file(icc_path("srgb_sampled_trc.icc"));
  ASSERT_FALSE(bytes.empty());
  GCOL_Limits lim;
  gcol_limits_default(&lim);
  lim.curve_samples = 16;
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, &lim, bytes.data(), bytes.size(), &p),
      GCOL_ERR_LIMIT);
}

TEST(IccToColorInfo, NamedGamutsRecover) {
  struct Case {
    const char * file;
    GCOL_Primaries primaries;
    GCOL_Transfer transfer;
    double gamma;
  };
  const Case cases[] = {
      {"srgb_g22.icc", GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2},
      {"adobergb_g22.icc", GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.2},
      {"displayp3_g22.icc", GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2},
      {"srgb_sampled_trc.icc", GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0},
  };
  for (const Case & c : cases) {
    GCOL_ICC_Profile * p = parse_named(c.file);
    ASSERT_NE(p, nullptr) << c.file;
    GCOL_Color_Info info;
    ASSERT_EQ(gcol_icc_to_color_info(p, &info), GCOL_OK) << c.file;
    EXPECT_EQ(gcol_gamut_identify(&info.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
        c.primaries)
        << c.file;
    EXPECT_EQ(info.transfer, c.transfer) << c.file;
    if (c.transfer == GCOL_TRANSFER_GAMMA) {
      EXPECT_NEAR(info.gamma_value, c.gamma, 1.0 / 256.0) << c.file;
    }
    gcol_icc_free(p);
  }
}

TEST(IccToColorInfo, GrayRecoversTransferOnly) {
  GCOL_ICC_Profile * p = parse_named("gray_g22.icc");
  ASSERT_NE(p, nullptr);
  GCOL_Color_Info info;
  ASSERT_EQ(gcol_icc_to_color_info(p, &info), GCOL_OK);
  EXPECT_FALSE(info.primaries_stated);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_GAMMA);
  EXPECT_NEAR(info.gamma_value, 2.2, 1.0 / 256.0);
  gcol_icc_free(p);
}

TEST(IccToColorInfo, SwapRgDoesNotIdentifyAsSrgb) {
  GCOL_ICC_Profile * p = parse_named("swap_rg.icc");
  ASSERT_NE(p, nullptr);
  GCOL_Color_Info info;
  ASSERT_EQ(gcol_icc_to_color_info(p, &info), GCOL_OK);
  EXPECT_TRUE(info.primaries_stated);
  EXPECT_EQ(info.transfer, GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(gcol_gamut_identify(&info.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
      GCOL_PRIMARIES_UNKNOWN);
  gcol_icc_free(p);
}

TEST(Transform, SwapRgTurnsRedIntoGreen) {
  GCOL_ICC_Profile * swap = parse_named("swap_rg.icc");
  GCOL_ICC_Profile * srgb = parse_named("srgb_g22.icc");
  ASSERT_NE(swap, nullptr);
  ASSERT_NE(srgb, nullptr);

  GCOL_Color_Info src, dst;
  ASSERT_EQ(gcol_icc_to_color_info(swap, &src), GCOL_OK);
  ASSERT_EQ(gcol_icc_to_color_info(srgb, &dst), GCOL_OK);
  /* Destination transfer: use sRGB piecewise so 8-bit encoding matches. */
  dst.transfer = GCOL_TRANSFER_SRGB;
  (void)gcol_transfer_conventions(
      GCOL_TRANSFER_SRGB, &dst.reference, &dst.sample_scale, &dst.white_luminance);

  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &src, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);
  ASSERT_NE(x, nullptr);

  uint8_t in[3] = {255, 0, 0};
  uint8_t out[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_UINT8, 3};
  ASSERT_EQ(gcol_transform_apply(x, &layout, in, &layout, out, 1), GCOL_OK);

  EXPECT_GT(out[1], 200) << "green channel";
  EXPECT_LT(out[0], 80) << "red channel";
  EXPECT_LT(out[2], 80) << "blue channel";

  gcol_transform_free(x);
  gcol_icc_free(swap);
  gcol_icc_free(srgb);
}

TEST(Transform, IdentityRoundTripUint8) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_color_info_set_gamut(&info, GCOL_PRIMARIES_SRGB));
  info.transfer = GCOL_TRANSFER_SRGB;
  (void)gcol_transfer_conventions(
      GCOL_TRANSFER_SRGB, &info.reference, &info.sample_scale, &info.white_luminance);

  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &info, &info,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);

  uint8_t in[3] = {64, 128, 192};
  uint8_t out[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_UINT8, 3};
  ASSERT_EQ(gcol_transform_apply(x, &layout, in, &layout, out, 1), GCOL_OK);
  /* Truncation on encode can lose at most 1 LSB around the round trip. */
  EXPECT_NEAR(out[0], in[0], 1);
  EXPECT_NEAR(out[1], in[1], 1);
  EXPECT_NEAR(out[2], in[2], 1);

  gcol_transform_free(x);
}

TEST(Transform, FloatLayoutWorks) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_color_info_set_gamut(&info, GCOL_PRIMARIES_SRGB));
  info.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &info, &info,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);

  float in[4] = {0.25f, 0.5f, 0.75f, 0.9f};
  float out[4] = {0, 0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 4};
  ASSERT_EQ(gcol_transform_apply(x, &layout, in, &layout, out, 1), GCOL_OK);
  EXPECT_NEAR(out[0], 0.25, 1e-6);
  EXPECT_NEAR(out[1], 0.5, 1e-6);
  EXPECT_NEAR(out[2], 0.75, 1e-6);
  EXPECT_NEAR(out[3], 0.9, 1e-6);

  gcol_transform_free(x);
}

TEST(IccWriteParse, RoundTripSrgbGamma) {
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_color_info_set_gamut(&info, GCOL_PRIMARIES_SRGB));
  info.transfer = GCOL_TRANSFER_GAMMA;
  info.gamma_value = 2.2;

  void * bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(gcol_icc_write(nullptr, &info, &bytes, &size), GCOL_OK);
  ASSERT_NE(bytes, nullptr);
  ASSERT_GT(size, 128u);

  GCOL_ICC_Profile * p = nullptr;
  ASSERT_EQ(gcol_icc_parse(nullptr, nullptr, bytes, size, &p), GCOL_OK);
  GCOL_Color_Info back;
  ASSERT_EQ(gcol_icc_to_color_info(p, &back), GCOL_OK);
  EXPECT_EQ(gcol_gamut_identify(&back.gamut, GCOL_GAMUT_TOLERANCE_DEFAULT),
      GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(back.transfer, GCOL_TRANSFER_GAMMA);
  EXPECT_NEAR(back.gamma_value, 2.2, 1.0 / 256.0);

  gcol_icc_free(p);
  gcol_icc_bytes_free(nullptr, bytes);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
