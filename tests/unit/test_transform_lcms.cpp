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
#include <ghoti.io/color/transform.h>

#include <cmath>
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

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
