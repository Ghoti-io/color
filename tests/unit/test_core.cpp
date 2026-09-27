/**
 * @file
 *
 * The result vocabulary, the version, and the allocator.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include <ghoti.io/cutil/allocator.h>
#include <ghoti.io/color/libver.h>

#include <set>
#include <string>

/* CONVENTIONS.md section 5: GCOL_RESULT_COUNT closes the enum so a test can
 * check the string table is complete. A new result without a string falls
 * through to "Unknown error" and is caught here. */
TEST(Result, EveryCodeHasItsOwnString) {
  std::set<std::string> seen;
  for (int i = 0; i < GCOL_RESULT_COUNT; i++) {
    const char * s = gcol_result_string(static_cast<GCOL_Result>(i));
    ASSERT_NE(s, nullptr);
    EXPECT_STRNE(s, "Unknown error") << "result " << i;
    EXPECT_TRUE(seen.insert(s).second) << "result " << i << " shares a string";
  }
}

TEST(Result, OutOfRangeIsUnknownNotUndefined) {
  EXPECT_STREQ(gcol_result_string(GCOL_RESULT_COUNT), "Unknown error");
  EXPECT_STREQ(gcol_result_string(static_cast<GCOL_Result>(-1)),
      "Unknown error");
}

TEST(Result, ZeroIsSuccess) {
  EXPECT_EQ(GCOL_OK, 0);
  EXPECT_STREQ(gcol_result_string(GCOL_OK), "No error");
}

/* CONVENTIONS.md section 5 fixes the vocabulary and says a new library adds
 * nothing to it without a reason.  This asserts that nothing was added: the
 * count is the list, so a constant appended without a documented reason fails
 * here rather than being noticed later. */
TEST(Result, TheVocabularyIsTheSuitesWithNothingAdded) {
  EXPECT_EQ(GCOL_RESULT_COUNT, 9);
}

TEST(Version, StringMatchesTheGeneratedHeader) {
  EXPECT_STREQ(gcol_version_string(), GCOL_VERSION_STRING);
  EXPECT_EQ(gcol_version_number(), GCOL_VERSION_NUMBER);
  EXPECT_EQ(GCOL_VERSION_NUMBER,
      GCOL_MAKE_VERSION(GCOL_VERSION_MAJOR, GCOL_VERSION_MINOR,
          GCOL_VERSION_PATCH));
}

TEST(Allocator, DefaultIsCutils) {
  const GCOL_Allocator * allocator = gcol_allocator_default();
  ASSERT_NE(allocator, nullptr);
  EXPECT_EQ(allocator, gcu_allocator_default());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
