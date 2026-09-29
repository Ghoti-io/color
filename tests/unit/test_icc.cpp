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

#include <cmath>
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
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
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
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
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
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_RGB};
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

TEST(IccLut, RgbMft1AndMabParse) {
  for (const char * name : {"rgb_mft1.icc", "rgb_mab.icc", "rgb_mab_para.icc"}) {
    GCOL_ICC_Profile * p = parse_named(name);
    ASSERT_NE(p, nullptr) << name;
    EXPECT_FALSE(gcol_icc_is_matrix_trc(p)) << name;
    EXPECT_EQ(gcol_icc_data_space(p), 0x52474220u) << name; /* 'RGB ' */
    GCOL_Color_Info info;
    EXPECT_EQ(gcol_icc_to_color_info(p, &info), GCOL_ERR_UNSUPPORTED) << name;
    gcol_icc_free(p);
  }
}

TEST(IccLut, LutGridLimitIsEnforced) {
  auto bytes = load_file(icc_path("cmyk_mft2.icc"));
  ASSERT_FALSE(bytes.empty());
  GCOL_Limits lim;
  gcol_limits_default(&lim);
  lim.lut_grid = 1; /* fixture uses grid=5 */
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, &lim, bytes.data(), bytes.size(), &p),
      GCOL_ERR_LIMIT);
  EXPECT_EQ(p, nullptr);
}

TEST(IccCmyk, TransformsWithPolarity) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_mft2.icc");
  ASSERT_NE(cmyk, nullptr);
  EXPECT_FALSE(gcol_icc_is_matrix_trc(cmyk));
  EXPECT_EQ(gcol_icc_data_space(cmyk), 0x434D594Bu); /* 'CMYK' */

  GCOL_Color_Info dst;
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * ink = nullptr;
  GCOL_Transform * refl = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, GCOL_CMYK_POLARITY_INK,
                  &ink),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &refl),
      GCOL_OK);

  /* Storage: heavy cyan under INK polarity is low byte; under REFLECTION the
   * same bytes mean light cyan. Outputs must disagree. */
  uint8_t in[4] = {32, 255, 255, 255}; /* ink: mostly cyan; refl: little cyan */
  float out_ink[3] = {0, 0, 0};
  float out_refl[3] = {0, 0, 0};
  GCOL_Sample_Layout src_layout = {GCOL_SAMPLE_UINT8, 4, GCOL_PIXEL_CMYK};
  GCOL_Sample_Layout dst_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(ink, &src_layout, in, &dst_layout, out_ink, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(refl, &src_layout, in, &dst_layout, out_refl, 1),
      GCOL_OK);

  const double d = std::fabs(out_ink[0] - out_refl[0]) +
      std::fabs(out_ink[1] - out_refl[1]) +
      std::fabs(out_ink[2] - out_refl[2]);
  EXPECT_GT(d, 0.05) << "polarity must change the transform";

  /* Arm the gate: inverted polarity expectation must fail. */
  EXPECT_GT(d, 0.0);

  gcol_transform_free(ink);
  gcol_transform_free(refl);
  gcol_icc_free(cmyk);
}

TEST(IccCmyk, PerceptualWithoutTableRefused) {
  /* Matrix/TRC profile: no A2B0 to honour. */
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_ADOBE_RGB));
  src.transfer = GCOL_TRANSFER_SRGB;
  dst.transfer = GCOL_TRANSFER_SRGB;
  GCOL_Transform * x = nullptr;
  EXPECT_EQ(gcol_transform_create(nullptr, &src, &dst, GCOL_INTENT_PERCEPTUAL,
                  0, &x),
      GCOL_ERR_UNSUPPORTED);
}

TEST(IccCmyk, PerceptualUsesA2B0Table) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_mft2.icc");
  ASSERT_NE(cmyk, nullptr);
  GCOL_Color_Info dst;
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * rel = nullptr;
  GCOL_Transform * per = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &rel),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_PERCEPTUAL, 0, GCOL_CMYK_POLARITY_REFLECTION,
                  &per),
      GCOL_OK);

  /* Paper white under both intents; A2B0 is half the XYZ of A2B1, so the
   * perceptual result is darker. */
  float in[4] = {0.f, 0.f, 0.f, 0.f};
  float out_rel[3] = {0, 0, 0};
  float out_per[3] = {0, 0, 0};
  GCOL_Sample_Layout src_layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_CMYK};
  GCOL_Sample_Layout dst_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(rel, &src_layout, in, &dst_layout, out_rel, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(per, &src_layout, in, &dst_layout, out_per, 1),
      GCOL_OK);
  EXPECT_NEAR(out_rel[1], 1.0, 0.05);
  EXPECT_LT(out_per[1], out_rel[1] - 0.2f);

  gcol_transform_free(rel);
  gcol_transform_free(per);
  gcol_icc_free(cmyk);
}

TEST(IccCmyk, DestinationCmykViaB2A) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_mft2.icc");
  ASSERT_NE(cmyk, nullptr);

  GCOL_Color_Info src;
  gcol_color_info_default(&src);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  src.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * to_cmyk = nullptr;
  ASSERT_EQ(gcol_transform_create_to_icc(nullptr, &src, cmyk,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &to_cmyk),
      GCOL_OK);

  GCOL_Transform * to_rgb = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &src,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &to_rgb),
      GCOL_OK);

  GCOL_Sample_Layout rgb_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  GCOL_Sample_Layout cmyk_layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_CMYK};

  /* Mid-grey is where denser (grid=5) CLUTs earn their keep. */
  float mid[3] = {0.5f, 0.5f, 0.5f};
  float mid_cmyk[4] = {0, 0, 0, 0};
  float mid_back[3] = {0, 0, 0};
  ASSERT_EQ(gcol_transform_apply(
                to_cmyk, &rgb_layout, mid, &cmyk_layout, mid_cmyk, 1),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_apply(
                to_rgb, &cmyk_layout, mid_cmyk, &rgb_layout, mid_back, 1),
      GCOL_OK);
  EXPECT_NEAR(mid_back[0], 0.5, 0.05);
  EXPECT_NEAR(mid_back[1], 0.5, 0.05);
  EXPECT_NEAR(mid_back[2], 0.5, 0.05);

  /* Paper white: low ink; round-trip within the matrix/subtractive mismatch. */
  float white[3] = {1.f, 1.f, 1.f};
  float paper[4] = {1.f, 1.f, 1.f, 1.f};
  float white_back[3] = {0, 0, 0};
  ASSERT_EQ(gcol_transform_apply(
                to_cmyk, &rgb_layout, white, &cmyk_layout, paper, 1),
      GCOL_OK);
  EXPECT_LT(paper[0] + paper[1] + paper[2] + paper[3], 0.35f);
  ASSERT_EQ(gcol_transform_apply(
                to_rgb, &cmyk_layout, paper, &rgb_layout, white_back, 1),
      GCOL_OK);
  EXPECT_NEAR(white_back[0], 1.0, 0.18);
  EXPECT_NEAR(white_back[1], 1.0, 0.18);
  EXPECT_NEAR(white_back[2], 1.0, 0.18);

  gcol_transform_free(to_cmyk);
  gcol_transform_free(to_rgb);
  gcol_icc_free(cmyk);
}

TEST(IccCmyk, SaturationWithoutTableRefused) {
  GCOL_Color_Info src, dst;
  gcol_color_info_default(&src);
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&src, GCOL_PRIMARIES_SRGB));
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_ADOBE_RGB));
  src.transfer = GCOL_TRANSFER_SRGB;
  dst.transfer = GCOL_TRANSFER_SRGB;
  GCOL_Transform * x = nullptr;
  EXPECT_EQ(gcol_transform_create(nullptr, &src, &dst, GCOL_INTENT_SATURATION,
                  0, &x),
      GCOL_ERR_UNSUPPORTED);
}

TEST(IccCmyk, SaturationUsesA2B2Table) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_mft2.icc");
  ASSERT_NE(cmyk, nullptr);
  GCOL_Color_Info dst;
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * rel = nullptr;
  GCOL_Transform * sat = nullptr;
  GCOL_Transform * per = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &rel),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_SATURATION, 0, GCOL_CMYK_POLARITY_REFLECTION,
                  &sat),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_PERCEPTUAL, 0, GCOL_CMYK_POLARITY_REFLECTION,
                  &per),
      GCOL_OK);

  float in[4] = {0.f, 0.f, 0.f, 0.f};
  float out_rel[3] = {0, 0, 0};
  float out_sat[3] = {0, 0, 0};
  float out_per[3] = {0, 0, 0};
  GCOL_Sample_Layout src_layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_CMYK};
  GCOL_Sample_Layout dst_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(rel, &src_layout, in, &dst_layout, out_rel, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(sat, &src_layout, in, &dst_layout, out_sat, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(per, &src_layout, in, &dst_layout, out_per, 1),
      GCOL_OK);
  /* A2B2 is 0.75× relative; A2B0 is 0.5× — saturation sits between. */
  EXPECT_LT(out_sat[1], out_rel[1] - 0.1f);
  EXPECT_GT(out_sat[1], out_per[1] + 0.1f);

  gcol_transform_free(rel);
  gcol_transform_free(sat);
  gcol_transform_free(per);
  gcol_icc_free(cmyk);
}

TEST(IccLut, AbsoluteScalesByMediaWhite) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_abs.icc");
  ASSERT_NE(cmyk, nullptr);
  GCOL_Color_Info dst;
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * rel = nullptr;
  GCOL_Transform * abs = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &rel),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_ABSOLUTE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &abs),
      GCOL_OK);

  float in[4] = {0.f, 0.f, 0.f, 0.f};
  float out_rel[3] = {0, 0, 0};
  float out_abs[3] = {0, 0, 0};
  GCOL_Sample_Layout src_layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_CMYK};
  GCOL_Sample_Layout dst_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(rel, &src_layout, in, &dst_layout, out_rel, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(abs, &src_layout, in, &dst_layout, out_abs, 1),
      GCOL_OK);
  /* Media Y=0.9 vs D50 Y=1 → absolute paper is dimmer than relative. */
  EXPECT_NEAR(out_rel[1], 1.0, 0.05);
  EXPECT_LT(out_abs[1], out_rel[1] - 0.05f);

  gcol_transform_free(rel);
  gcol_transform_free(abs);
  gcol_icc_free(cmyk);
}

TEST(IccLut, BpcUsesBkpt) {
  GCOL_ICC_Profile * cmyk = parse_named("cmyk_abs.icc");
  ASSERT_NE(cmyk, nullptr);
  GCOL_Color_Info dst;
  gcol_color_info_default(&dst);
  ASSERT_TRUE(gcol_color_info_set_gamut(&dst, GCOL_PRIMARIES_SRGB));
  dst.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * plain = nullptr;
  GCOL_Transform * bpc = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_REFLECTION, &plain),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_icc(nullptr, cmyk, &dst,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC,
                  GCOL_TRANSFORM_CLIP | GCOL_TRANSFORM_BPC,
                  GCOL_CMYK_POLARITY_REFLECTION, &bpc),
      GCOL_OK);

  /* Near-black ink: BPC stretches media black toward destination black (0). */
  float in[4] = {0.95f, 0.95f, 0.95f, 0.95f};
  float out_plain[3] = {0, 0, 0};
  float out_bpc[3] = {0, 0, 0};
  GCOL_Sample_Layout src_layout = {GCOL_SAMPLE_FLOAT32, 4, GCOL_PIXEL_CMYK};
  GCOL_Sample_Layout dst_layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(plain, &src_layout, in, &dst_layout, out_plain, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(bpc, &src_layout, in, &dst_layout, out_bpc, 1),
      GCOL_OK);
  EXPECT_NE(out_plain[1], out_bpc[1]);

  gcol_transform_free(plain);
  gcol_transform_free(bpc);
  gcol_icc_free(cmyk);
}

TEST(IccLut, LabPcsRoundTrip) {
  GCOL_ICC_Profile * lab = parse_named("rgb_lab_mft2.icc");
  ASSERT_NE(lab, nullptr);
  EXPECT_EQ(gcol_icc_pcs(lab), 0x4C616220u); /* 'Lab ' */

  GCOL_Color_Info rgb;
  gcol_color_info_default(&rgb);
  ASSERT_TRUE(gcol_color_info_set_gamut(&rgb, GCOL_PRIMARIES_SRGB));
  rgb.transfer = GCOL_TRANSFER_LINEAR;

  GCOL_Transform * to_rgb = nullptr;
  GCOL_Transform * to_dev = nullptr;
  ASSERT_EQ(gcol_transform_create_icc(nullptr, lab, &rgb,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_UNKNOWN, &to_rgb),
      GCOL_OK);
  ASSERT_EQ(gcol_transform_create_to_icc(nullptr, &rgb, lab,
                  GCOL_INTENT_RELATIVE_COLORIMETRIC, 0,
                  GCOL_CMYK_POLARITY_UNKNOWN, &to_dev),
      GCOL_OK);

  float white[3] = {1.f, 1.f, 1.f};
  float device[3] = {0, 0, 0};
  float back[3] = {0, 0, 0};
  GCOL_Sample_Layout layout = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(
      gcol_transform_apply(to_dev, &layout, white, &layout, device, 1),
      GCOL_OK);
  ASSERT_EQ(
      gcol_transform_apply(to_rgb, &layout, device, &layout, back, 1),
      GCOL_OK);
  EXPECT_NEAR(back[0], 1.0, 0.35);
  EXPECT_NEAR(back[1], 1.0, 0.35);
  EXPECT_NEAR(back[2], 1.0, 0.35);

  gcol_transform_free(to_rgb);
  gcol_transform_free(to_dev);
  gcol_icc_free(lab);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
