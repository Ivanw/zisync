/**
 * @file test_reporter_dox_stdout.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief TestReportDoxStdout.
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

#include <UnitTest++/TestDetails.h>
#include "test_reporter_dox_stdout.h"

namespace zs {

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

const char* to_cstr(std::string const& text) {
  return text.c_str();
}

std::string splitCamelCaseWord(std::string const& text) {
  std::string sentence;
  for (auto pos = text.begin(); pos != text.end(); ++pos) {
    if (isupper(*pos) && pos != text.begin()) {
      sentence.append(1, ' ');
    }
    if (pos == text.begin()) {
      sentence.append(1, toupper(*pos));
    } else {
      sentence.append(1, tolower(*pos));
    }
  }
  return sentence;
}

void TestReporterDoxStdout::ReportTestResult(TestDetails const &test, bool success) {
  // if (test.suiteName != m_suiteName) {
  //   m_suiteName = test.suiteName;
  //   printf("\n%s\n", to_cstr(m_suiteName));
  // }
  // printf(" [%c] %s\n", success ? 'x':' ', to_cstr(splitCamelCaseWord(test.testName)));
}

void TestReporterDoxStdout::ReportFailure(TestDetails const& details, char const* failure) {
#if defined(__APPLE__) || defined(__GNUG__)
  char const* const errorFormat = "%s%s:%d: error: Failure in %s: %s%s\n";
#else
  char const* const errorFormat = "%s%s(%d): error: Failure in %s: %s%s\n";
#endif

  using namespace std;
  printf("\r%sRunning case: %s%-40s %s(#.## seconds) %s[%s fail %s]\n",
         KNRM, KRED, details.testName, KNRM, KBLU, KRED, KBLU);
  printf(errorFormat, KRED, details.filename, details.lineNumber, details.testName, failure, KNRM);
}

void TestReporterDoxStdout::ReportTestStart(TestDetails const& test) {
  printf("%sRunning case: %s%-40s %s(#.## seconds) %s[%s......%s]",
         KNRM, KBLU, test.testName, KNRM, KBLU, KNRM, KBLU);
  fflush(stdout);
}

void TestReporterDoxStdout::ReportTestFinish(TestDetails const& test, float secondsElapsed) {
  // ReportTestResult(test, true);
  printf("\r%sRunning case: %s%-40s %s(%s%.2f seconds%s) %s[%s done %s]\n",
         KNRM, KBLU, test.testName, KNRM, KCYN, secondsElapsed, KNRM, KBLU, KGRN, KBLU);
}

void TestReporterDoxStdout::ReportSummary(
    int const totalTestCount, int const failedTestCount,
    int const failureCount, float secondsElapsed) {
  using namespace std;
  if (failureCount > 0) {
    printf("FAILURE: %d out of %d tests failed (%d failures).\n",
           failedTestCount, totalTestCount, failureCount);
  } else {
    printf("Success: %d tests passed.\n", totalTestCount);
  }

  printf("Test time: %.2f seconds.\n", secondsElapsed);
}


}  // namespace zs
