/**
 * @file
 *
 * libFuzzer harness for gcol_icc_parse.
 *
 * Build with: make fuzz-icc_parse
 * Run:        make fuzz-run-icc_parse FUZZ_TIME=60
 *
 * Seed corpus: tests/fuzz/corpus/icc_parse/ (the six ICC fixtures).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ghoti.io/color/icc.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (data == nullptr) {
    return 0;
  }

  GCOL_Limits lim;
  gcol_limits_default(&lim);
  /* Keep fuzz allocations bounded. */
  if (size > 0) {
    lim.tag_count = 32u + (size_t)(data[0] % 64);
    lim.curve_samples = 64u + (size_t)((data[0] % 16) * 64u);
    lim.profile_bytes = size + 1u;
  }

  GCOL_ICC_Profile * profile = nullptr;
  GCOL_Result r = gcol_icc_parse(nullptr, &lim, data, size, &profile);
  if (r == GCOL_OK && profile != nullptr) {
    GCOL_Color_Info info;
    (void)gcol_icc_to_color_info(profile, &info);
    unsigned char id[16];
    (void)gcol_icc_profile_id(profile, id);
    (void)gcol_icc_description(profile);
    (void)gcol_icc_is_matrix_trc(profile);
    gcol_icc_free(profile);
  }
  return 0;
}
