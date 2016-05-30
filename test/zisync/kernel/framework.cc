/**
 * @file test_database.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief main() for database related testing.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */
#include <cassert>
#include <cstring>
#include <UnitTest++/UnitTest++.h>
#include <UnitTest++/TestReporterStdout.h>
#include "zisync_kernel.h"  // NOLINT

class Predicate
{
 public:

  Predicate(const char *prefix)
      : prefix_(prefix), prefix_len_(strlen(prefix)) {}

  bool operator()(UnitTest::Test *test) const
  {
    return strncmp(test->m_details.testName, prefix_, prefix_len_) == 0;
  }
 private:
  const char *prefix_;
  size_t prefix_len_;
};

int main(int argc, char** argv ) {
  zs::DefaultLogger logger("./Log");
  logger.Initialize();
  logger.error_to_stderr = true;
  LogInitialize(&logger);
  if (argc == 1) {
    UnitTest::RunAllTests();
  } else if (argc == 2) {
    char *prefix = argv[1];
    Predicate predicate(prefix);
    UnitTest::TestReporterStdout reporter;
    UnitTest::TestRunner runner(reporter);
    runner.RunTestsIf(
        UnitTest::Test::GetTestList(), NULL, predicate, 0);
  } else {
    assert(false);
  }
  logger.CleanUp();
  return 0;
}

