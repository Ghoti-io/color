/**
 * @file
 *
 * SGI LogLuv against libtiff's answers.
 *
 * tests/data/logluv/ is what libtiff 4.7.0 decodes each sampled code to and
 * encodes each sampled XYZ to (tools/oracle/gen_logluv.py, `make
 * logluv-vectors`).  Decoding is compared at float precision, because that is
 * what libtiff hands back; encoding must match code for code.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/color/math.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::vector<std::string>> load(const char * name) {
  std::vector<std::vector<std::string>> rows;
  std::ifstream in(std::string(GCOL_TEST_DATA "/logluv/") + name);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream words(line);
    std::vector<std::string> row;
    std::string w;
    while (words >> w) {
      row.push_back(w);
    }
    rows.push_back(row);
  }
  return rows;
}

uint32_t hex(const std::string & s) {
  return (uint32_t)std::strtoul(s.c_str(), nullptr, 16);
}

// libtiff returns float; ours is double.  Equal once both are float, within
// a couple of ulps for the exp2 each side calls.
bool same_float(double ours, double theirs) {
  const float a = (float)ours, b = (float)theirs;
  if (a == b) {
    return true;
  }
  return std::fabs(a - b) <= 4.0f * std::fabs(b) * 1.1920929e-7f;
}

void expect_xyz(const char * file, bool wide) {
  const auto rows = load(file);
  ASSERT_GT(rows.size(), 1000u) << file;
  size_t bad = 0;
  for (const auto & r : rows) {
    ASSERT_EQ(r.size(), 4u);
    double xyz[3];
    const uint32_t code = hex(r[0]);
    ASSERT_EQ(wide ? gcol_logluv32_to_xyz(code, xyz)
                   : gcol_logluv24_to_xyz(code, xyz),
        GCOL_OK);
    for (int c = 0; c < 3; c++) {
      const double want = std::strtod(r[1 + c].c_str(), nullptr);
      if (!same_float(xyz[c], want)) {
        if (bad++ < 5) {
          ADD_FAILURE() << file << " " << r[0] << " channel " << c << ": "
                        << xyz[c] << " vs libtiff " << want;
        }
      }
    }
  }
  EXPECT_EQ(bad, 0u) << file;
}

// Whether libtiff's 24-bit code puts the input's u'v' inside the cell it
// names: the samples where the input was inside the grid, which must match
// exactly.  The rest were outside it and are placed differently on purpose.
bool inside_named_cell(const double xyz[3], uint32_t code) {
  const double s = xyz[0] + 15.0 * xyz[1] + 3.0 * xyz[2];
  if (!(s > 0.0) || (code >> 14) == 0u) {
    return true; // Undefined chromaticity: both write white.
  }
  double cell[3];
  if (gcol_logluv24_to_xyz((768u << 14) | (code & 0x3fffu), cell) != GCOL_OK) {
    return false;
  }
  const double cs = cell[0] + 15.0 * cell[1] + 3.0 * cell[2];
  const double du = 4.0 * xyz[0] / s - 4.0 * cell[0] / cs;
  const double dv = 9.0 * xyz[1] / s - 9.0 * cell[1] / cs;
  return std::fabs(du) <= 0.00175 + 1e-6 && std::fabs(dv) <= 0.00175 + 1e-6;
}

void expect_codes(const char * file, int which) {
  const auto rows = load(file);
  ASSERT_GT(rows.size(), 1000u) << file;
  size_t bad = 0, outside = 0;
  for (const auto & r : rows) {
    uint32_t got = 0;
    if (which == 16) {
      ASSERT_EQ(r.size(), 2u);
      got = gcol_y_to_logl16((double)std::strtof(r[0].c_str(), nullptr));
    }
    else {
      ASSERT_EQ(r.size(), 4u);
      const double xyz[3] = {(double)std::strtof(r[0].c_str(), nullptr),
          (double)std::strtof(r[1].c_str(), nullptr),
          (double)std::strtof(r[2].c_str(), nullptr)};
      ASSERT_EQ(which == 32 ? gcol_xyz_to_logluv32(xyz, &got)
                            : gcol_xyz_to_logluv24(xyz, &got),
          GCOL_OK);
    }
    const uint32_t want = hex(r.back());
    if (which == 24) {
      const double xyz[3] = {(double)std::strtof(r[0].c_str(), nullptr),
          (double)std::strtof(r[1].c_str(), nullptr),
          (double)std::strtof(r[2].c_str(), nullptr)};
      if (!inside_named_cell(xyz, want)) {
        outside++;
        // Same luminance; a real cell, never past the table.
        EXPECT_EQ(got >> 14, want >> 14) << r[0];
        double back[3];
        EXPECT_EQ(gcol_logluv24_to_xyz(got, back), GCOL_OK);
        EXPECT_LT(got & 0x3fffu, 16289u) << r[0];
        continue;
      }
    }
    if (got != want) {
      if (bad++ < 5) {
        ADD_FAILURE() << file << " " << r[0] << " ...: " << std::hex << got
                      << " vs libtiff " << want;
      }
    }
  }
  EXPECT_EQ(bad, 0u) << file;
  if (which == 24) {
    // The out-of-grid branch is exercised, not just written.
    EXPECT_GT(outside, 50u);
  }
}

} // namespace

TEST(LogLuvOracle, LogL16DecodesAsLibtiff) {
  const auto rows = load("dec_l16.txt");
  ASSERT_GT(rows.size(), 1000u);
  size_t bad = 0;
  for (const auto & r : rows) {
    const double want = std::strtod(r[1].c_str(), nullptr);
    const double got = gcol_logl16_to_y((uint16_t)hex(r[0]));
    if (!same_float(got, want) && bad++ < 5) {
      ADD_FAILURE() << r[0] << ": " << got << " vs libtiff " << want;
    }
  }
  EXPECT_EQ(bad, 0u);
}

TEST(LogLuvOracle, LogLuv32DecodesAsLibtiff) {
  expect_xyz("dec_luv32.txt", true);
}

TEST(LogLuvOracle, LogLuv24DecodesAsLibtiff) {
  expect_xyz("dec_luv24.txt", false);
}

TEST(LogLuvOracle, LogL16EncodesAsLibtiff) {
  expect_codes("enc_l16.txt", 16);
}

TEST(LogLuvOracle, LogLuv32EncodesAsLibtiff) {
  expect_codes("enc_luv32.txt", 32);
}

TEST(LogLuvOracle, LogLuv24EncodesAsLibtiff) {
  expect_codes("enc_luv24.txt", 24);
}

// The defect this file was written for was a u'v' scale of 256 instead of
// 410: X and Y came out right (they depend on u'/v'), Z did not, and a round
// trip through an encoder with the same constant could not see it.  Only the
// oracle comparisons above can; this pins one pixel of libtiffpic's
// off_luv32.tif to what libtiff decodes it to, so the point survives a
// regeneration that drops the code from the sample.
TEST(LogLuv, ARealPixelKeepsItsZ) {
  double xyz[3];
  ASSERT_EQ(gcol_logluv32_to_xyz(0x3c1a56c2u, xyz), GCOL_OK);
  EXPECT_FLOAT_EQ((float)xyz[0], 0.0671924278f);
  EXPECT_FLOAT_EQ((float)xyz[1], 0.0671492741f);
  EXPECT_FLOAT_EQ((float)xyz[2], 0.0665019453f);
}

// LogL16 keeps the sign; LogLuv32 decodes a negative Y as black, which is
// what libtiff answers (dec_luv32.txt carries the 0x8001.. codes).
TEST(LogLuv, SignBitIsTheSignOfY) {
  EXPECT_GT(gcol_logl16_to_y(0x4000u), 0.0);
  EXPECT_EQ(gcol_logl16_to_y(0xc000u), -gcol_logl16_to_y(0x4000u));
  EXPECT_EQ(gcol_logl16_to_y(0x8000u), 0.0);
  double xyz[3] = {1, 1, 1};
  ASSERT_EQ(gcol_logluv32_to_xyz(0xc00056c2u, xyz), GCOL_OK);
  EXPECT_EQ(xyz[0], 0.0);
  EXPECT_EQ(xyz[1], 0.0);
  EXPECT_EQ(xyz[2], 0.0);
}

TEST(LogLuv, NullArgumentsAreRefused) {
  double xyz[3] = {1, 1, 1};
  uint32_t code = 0;
  EXPECT_EQ(gcol_logluv32_to_xyz(0, nullptr), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_logluv24_to_xyz(0, nullptr), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_xyz_to_logluv32(nullptr, &code), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_xyz_to_logluv32(xyz, nullptr), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_xyz_to_logluv24(nullptr, &code), GCOL_ERR_INVALID);
  EXPECT_EQ(gcol_xyz_to_logluv24(xyz, nullptr), GCOL_ERR_INVALID);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
