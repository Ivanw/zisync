/**
 * @file test_reporter_dox_stdout.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief TestReporterDoxStdout.
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

#ifndef TEST_REPORTER_DOX_STDOUT_H
#define TEST_REPORTER_DOX_STDOUT_H

#include <UnitTest++/TestReporter.h>
#include <string>

namespace zs {

using UnitTest::TestReporter;
using UnitTest::TestDetails;

class TestReporterDoxStdout : public TestReporter {
 public:
  virtual void ReportTestStart(TestDetails const& test);
  virtual void ReportFailure(TestDetails const& test, char const* failure);
  virtual void ReportTestFinish(TestDetails const& test, float secondsElapsed);

  virtual void ReportSummary(
      int total_count, int failed_count,
      int failure_count, float seconds_elapsed);

  void ReportTestResult(TestDetails const &test, bool success);

  std::string m_suiteName;
};

}  // namespace zs

#endif
