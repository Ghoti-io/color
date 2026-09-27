/**
 * @file
 *
 * Shared helpers for the Ghoti.io Color unit tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GCOL_TEST_HELPERS_H
#define GHOTI_IO_GCOL_TEST_HELPERS_H

#include <gtest/gtest.h>

#include <ghoti.io/color/color.h>

#include <cstdlib>
#include <string>

namespace gcoltest {

/** Directory holding the checked-in vectors. The Makefile bakes in
 *  GCOL_TEST_DATA so the binaries can run from the build tree. */
inline std::string data_dir() {
  const char * env = std::getenv("GCOL_TEST_DATA");
  if (env != nullptr && env[0] != '\0') {
    return std::string(env);
  }
#ifdef GCOL_TEST_DATA
  return std::string(GCOL_TEST_DATA);
#else
  return std::string("tests/data");
#endif
}

} /* namespace gcoltest */

#endif /* GHOTI_IO_GCOL_TEST_HELPERS_H */
