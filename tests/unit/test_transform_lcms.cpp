/**
 * @file
 *
 * gcol_transform_apply against littleCMS 2.16's answers.
 *
 * tests/data/transform/lcms.txt is what the pinned littleCMS says for the
 * same transforms (tools/oracle/gen_transform_vectors.py, `make
 * transform-vectors`), so these compare two CMMs rather than this one with
 * itself.  The transforms are relative colorimetric and the profiles are
 * matrix/TRC, where two correct implementations differ only by rounding:
 * float results are held to 2e-4 (littleCMS's sRGB colorants are its own
 * D50 adaptation, and its curves are evaluated in its own arithmetic), and
 * 8-bit results to one code.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/color/color.h>
#include <ghoti.io/color/icc.h>
#include <ghoti.io/color/transform.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

/** Section name -> lines of (inputs, outputs). */
struct Line {
  std::vector<double> in, out;
};

std::map<std::string, std::vector<Line>> vectors() {
  std::map<std::string, std::vector<Line>> out;
  std::ifstream f(std::string(GCOL_TEST_DATA "/transform/lcms.txt"));
  std::string line, section;
  while (std::getline(f, line)) {
    if (line.rfind("## ", 0) == 0 || line.empty()) {
      continue;
    }
    if (line.rfind("# ", 0) == 0) {
      section = line.substr(2);
      continue;
    }
    const size_t arrow = line.find("->");
    if (arrow == std::string::npos) {
      continue;
    }
    Line l;
    std::istringstream a(line.substr(0, arrow)), b(line.substr(arrow + 2));
    double v;
    while (a >> v) {
      l.in.push_back(v);
    }
    while (b >> v) {
      l.out.push_back(v);
    }
    out[section].push_back(l);
  }
  return out;
}

GCOL_Color_Info srgb() {
  GCOL_Color_Info i;
  gcol_color_info_default(&i);
  EXPECT_TRUE(gcol_color_info_set_gamut(&i, GCOL_PRIMARIES_SRGB));
  i.transfer = GCOL_TRANSFER_SRGB;
  return i;
}

GCOL_Color_Info linear_srgb() {
  GCOL_Color_Info i = srgb();
  i.transfer = GCOL_TRANSFER_LINEAR;
  return i;
}

} // namespace

// An identity transform must be the identity at 8 bits.  Truncating the
// final multiply by 255 moved 44 of the 256 codes of a gray ramp down by
// one, because a code that round-trips through the curves as 4.9999 is 4.
TEST(TransformLcms, Srgb8IdentityLeavesEveryCodeAlone) {
  GCOL_Color_Info s = srgb();
  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &s, &s,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);
  std::vector<unsigned char> in(256 * 3), out(256 * 3);
  for (int i = 0; i < 256 * 3; i++) {
    in[i] = (unsigned char)(i / 3);
  }
  const GCOL_Sample_Layout l = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
  ASSERT_EQ(gcol_transform_apply(x, &l, in.data(), &l, out.data(), 256),
      GCOL_OK);
  int changed = 0;
  for (int i = 0; i < 256 * 3; i++) {
    changed += in[i] != out[i];
  }
  EXPECT_EQ(changed, 0);
  gcol_transform_free(x);
}

// Float into 8 bits rounds, as littleCMS does: 0.00646 of full scale is code
// 2, not the 1 a truncation gives.
TEST(TransformLcms, FloatToEightBitsRoundsAsLittleCmsDoes) {
  const auto v = vectors();
  const auto & lines = v.at("float_to_u8");
  ASSERT_GT(lines.size(), 8u);
  GCOL_Color_Info a = linear_srgb(), b = srgb();
  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &a, &b,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);
  const GCOL_Sample_Layout lf = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  const GCOL_Sample_Layout l8 = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
  for (const Line & l : lines) {
    const float in[3] = {(float)l.in[0], (float)l.in[1], (float)l.in[2]};
    unsigned char out[3];
    ASSERT_EQ(gcol_transform_apply(x, &lf, in, &l8, out, 1), GCOL_OK);
    for (int c = 0; c < 3; c++) {
      EXPECT_EQ((int)out[c], (int)l.out[c])
          << l.in[0] << " " << l.in[1] << " " << l.in[2] << " channel " << c;
    }
  }
  gcol_transform_free(x);
}

namespace {

bool near(double got, double want) {
  return std::fabs(got - want) <= 2e-4 * std::fmax(1.0, std::fabs(want));
}

} // namespace

// Float in, float out: nothing clipped.  Linear 2.0 is sRGB 1.353, 179 is
// 9.106, -0.1 is -1.292 - the curve continued past both ends, which is what
// littleCMS answers and what a scene-referred raster needs from a transform.
TEST(TransformLcms, FloatTransformsAreUnboundedAsLittleCmsIs) {
  const auto v = vectors();
  const auto & lines = v.at("float_unbounded");
  ASSERT_GT(lines.size(), 8u);
  GCOL_Color_Info a = linear_srgb(), b = srgb();
  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &a, &b,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);
  const GCOL_Sample_Layout lf = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  size_t outside = 0;
  for (const Line & l : lines) {
    const float in[3] = {(float)l.in[0], (float)l.in[1], (float)l.in[2]};
    float out[3];
    ASSERT_EQ(gcol_transform_apply(x, &lf, in, &lf, out, 1), GCOL_OK);
    for (int c = 0; c < 3; c++) {
      EXPECT_TRUE(near(out[c], l.out[c]))
          << l.in[0] << " " << l.in[1] << " " << l.in[2] << " channel " << c
          << ": " << out[c] << " vs littleCMS " << l.out[c];
      outside += (l.out[c] > 1.0 || l.out[c] < 0.0) ? 1u : 0u;
    }
  }
  EXPECT_GT(outside, 6u); // The section is about values past 0..1.
  gcol_transform_free(x);
}

// GCOL_TRANSFORM_CLIP still clips a float destination, for a caller who
// wants display-range floats.
TEST(TransformLcms, ClipFlagClipsFloatToo) {
  GCOL_Color_Info a = linear_srgb(), b = srgb();
  GCOL_Transform * x = nullptr;
  ASSERT_EQ(gcol_transform_create(nullptr, &a, &b,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, GCOL_TRANSFORM_CLIP, &x),
      GCOL_OK);
  const GCOL_Sample_Layout lf = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  const float in[3] = {2.0f, -0.1f, 0.5f};
  float out[3];
  ASSERT_EQ(gcol_transform_apply(x, &lf, in, &lf, out, 1), GCOL_OK);
  EXPECT_EQ(out[0], 1.0f);
  EXPECT_EQ(out[1], 0.0f);
  EXPECT_NEAR(out[2], 0.735357, 2e-4);
  gcol_transform_free(x);
}

namespace {

/** gray_g22.icc as a description: white and transfer, no primaries. */
GCOL_Color_Info gray_g22() {
  GCOL_Color_Info i;
  gcol_color_info_default(&i);
  std::ifstream f(std::string(GCOL_TEST_DATA "/icc/gray_g22.icc"),
      std::ios::binary);
  const std::vector<unsigned char> b(
      (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  GCOL_ICC_Profile * p = nullptr;
  EXPECT_EQ(gcol_icc_parse(nullptr, nullptr, b.data(), b.size(), &p),
      GCOL_OK);
  if (p) {
    EXPECT_EQ(gcol_icc_to_color_info(p, &i), GCOL_OK);
    gcol_icc_free(p);
  }
  EXPECT_FALSE(i.primaries_stated);
  EXPECT_TRUE(i.white_stated);
  return i;
}

GCOL_Transform * make(const GCOL_Color_Info & a, GCOL_Pixel_Model am,
    const GCOL_Color_Info & b, GCOL_Pixel_Model bm) {
  GCOL_Transform * x = nullptr;
  EXPECT_EQ(gcol_transform_create_model(nullptr, &a, am, &b, bm,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &x),
      GCOL_OK);
  return x;
}

} // namespace

// Every 8-bit code of a gamma-2.2 gray profile into sRGB, against
// littleCMS: a neutral, within one code.
TEST(TransformLcms, GrayToSrgbMatchesLittleCms) {
  const auto v = vectors();
  const auto & lines = v.at("gray_to_srgb_u8");
  ASSERT_EQ(lines.size(), 256u);
  GCOL_Transform * x =
      make(gray_g22(), GCOL_PIXEL_GRAY, srgb(), GCOL_PIXEL_RGB);
  ASSERT_NE(x, nullptr);
  const GCOL_Sample_Layout lg = {GCOL_SAMPLE_UINT8, 1, GCOL_PIXEL_GRAY};
  const GCOL_Sample_Layout lr = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
  int worst = 0;
  for (const Line & l : lines) {
    const unsigned char in = (unsigned char)l.in[0];
    unsigned char out[3];
    ASSERT_EQ(gcol_transform_apply(x, &lg, &in, &lr, out, 1), GCOL_OK);
    for (int c = 0; c < 3; c++) {
      worst = std::max(worst, std::abs((int)out[c] - (int)l.out[c]));
    }
  }
  EXPECT_LE(worst, 1);
  gcol_transform_free(x);
}

// sRGB colours into a gray destination are their luminance: neutrals,
// primaries and mixtures, against littleCMS within one code.
TEST(TransformLcms, SrgbToGrayIsLuminanceAsLittleCmsHasIt) {
  const auto v = vectors();
  const auto & lines = v.at("srgb_to_gray_u8");
  ASSERT_GT(lines.size(), 20u);
  GCOL_Transform * x =
      make(srgb(), GCOL_PIXEL_RGB, gray_g22(), GCOL_PIXEL_GRAY);
  ASSERT_NE(x, nullptr);
  const GCOL_Sample_Layout lg = {GCOL_SAMPLE_UINT8, 1, GCOL_PIXEL_GRAY};
  const GCOL_Sample_Layout lr = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
  for (const Line & l : lines) {
    const unsigned char in[3] = {(unsigned char)l.in[0],
        (unsigned char)l.in[1], (unsigned char)l.in[2]};
    unsigned char out = 0;
    ASSERT_EQ(gcol_transform_apply(x, &lr, in, &lg, &out, 1), GCOL_OK);
    EXPECT_LE(std::abs((int)out - (int)l.out[0]), 1)
        << l.in[0] << " " << l.in[1] << " " << l.in[2] << ": " << (int)out
        << " vs littleCMS " << l.out[0];
  }
  gcol_transform_free(x);
}

// Gray float into sRGB float, past 1 as well.  One line is left out by
// name: littleCMS evaluates a pure gamma curve at a negative sample as 0,
// and this library's GAMMA mirrors the sign (-(-v)^g), so gray -0.2 is
// black there and a negative linear value here.  Both are conventions; the
// sRGB curve, where the two agree, is the one the float section above pins.
TEST(TransformLcms, GrayFloatIsUnboundedAsLittleCmsIs) {
  const auto v = vectors();
  const auto & lines = v.at("gray_to_srgb_f");
  ASSERT_GT(lines.size(), 6u);
  GCOL_Transform * x =
      make(gray_g22(), GCOL_PIXEL_GRAY, srgb(), GCOL_PIXEL_RGB);
  ASSERT_NE(x, nullptr);
  const GCOL_Sample_Layout lg = {GCOL_SAMPLE_FLOAT32, 1, GCOL_PIXEL_GRAY};
  const GCOL_Sample_Layout lr = {GCOL_SAMPLE_FLOAT32, 3, GCOL_PIXEL_RGB};
  size_t compared = 0, above = 0;
  for (const Line & l : lines) {
    if (l.in[0] < 0.0) {
      continue;
    }
    const float in = (float)l.in[0];
    float out[3];
    ASSERT_EQ(gcol_transform_apply(x, &lg, &in, &lr, out, 1), GCOL_OK);
    for (int c = 0; c < 3; c++) {
      EXPECT_TRUE(near(out[c], l.out[c]))
          << l.in[0] << " channel " << c << ": " << out[c]
          << " vs littleCMS " << l.out[c];
    }
    above += l.out[0] > 1.0 ? 1u : 0u;
    compared++;
  }
  EXPECT_EQ(compared, lines.size() - 1u);
  EXPECT_GE(above, 2u);
  gcol_transform_free(x);
}

// Gray and alpha: the second channel is copied through, both ways, and a
// gray-to-gray transform between the same description is the identity.
TEST(TransformLcms, GrayAlphaIsCopiedAndGrayToGrayIsIdentity) {
  const GCOL_Color_Info g = gray_g22();
  GCOL_Transform * x = make(g, GCOL_PIXEL_GRAY, g, GCOL_PIXEL_GRAY);
  ASSERT_NE(x, nullptr);
  const GCOL_Sample_Layout la = {GCOL_SAMPLE_UINT8, 2, GCOL_PIXEL_GRAY};
  std::vector<unsigned char> in(512), out(512);
  for (int i = 0; i < 256; i++) {
    in[2 * i] = (unsigned char)i;
    in[2 * i + 1] = (unsigned char)(255 - i);
  }
  ASSERT_EQ(gcol_transform_apply(x, &la, in.data(), &la, out.data(), 256),
      GCOL_OK);
  EXPECT_EQ(in, out);
  gcol_transform_free(x);

  x = make(g, GCOL_PIXEL_GRAY, srgb(), GCOL_PIXEL_RGB);
  const GCOL_Sample_Layout lr = {GCOL_SAMPLE_UINT8, 4, GCOL_PIXEL_RGB};
  const unsigned char ga[2] = {128, 77};
  unsigned char rgba[4] = {0, 0, 0, 0};
  ASSERT_EQ(gcol_transform_apply(x, &la, ga, &lr, rgba, 1), GCOL_OK);
  EXPECT_EQ(rgba[3], 77);
  gcol_transform_free(x);
}

// A layout whose model does not match the side it is for is refused, and so
// is a gray endpoint used as RGB (no primaries).
TEST(TransformLcms, GrayModelsAreCheckedAgainstTheirSide) {
  const GCOL_Color_Info g = gray_g22();
  GCOL_Transform * x = make(g, GCOL_PIXEL_GRAY, srgb(), GCOL_PIXEL_RGB);
  ASSERT_NE(x, nullptr);
  const GCOL_Sample_Layout lr = {GCOL_SAMPLE_UINT8, 3, GCOL_PIXEL_RGB};
  unsigned char in[3] = {1, 2, 3}, out[3];
  EXPECT_EQ(gcol_transform_apply(x, &lr, in, &lr, out, 1), GCOL_ERR_INVALID);
  gcol_transform_free(x);
  GCOL_Transform * y = nullptr;
  EXPECT_EQ(gcol_transform_create(nullptr, &g, &g,
                GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &y),
      GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_transform_create_model(nullptr, &g, GCOL_PIXEL_CMYK, &g,
                GCOL_PIXEL_GRAY, GCOL_INTENT_RELATIVE_COLORIMETRIC, 0, &y),
      GCOL_ERR_INVALID);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
