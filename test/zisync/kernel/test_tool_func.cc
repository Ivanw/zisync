/**
 * @file test_tool_func.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implment tool function used for testing.
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
#include <stdio.h>
#include <UnitTest++/UnitTest++.h>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sha.h"
#include "test_tool_func.h"

namespace zs {

bool FileIsEqual(const std::string expected, const std::string actual) {
  std::string expected_sha1;
  std::string actual_sha1;

  err_t rc = FileSha1(expected, string(), &expected_sha1);
  CHECK(rc == ZISYNC_SUCCESS);

  rc = FileSha1(actual, string(), &actual_sha1);
  CHECK(rc == ZISYNC_SUCCESS);

  return expected_sha1 == actual_sha1;
  
  // FILE *fp_expected = NULL;
  // FILE *fp_actual = NULL; 
  // char buf_actual[1024] = {0};
  // char buf_expected[1024] = {0};

  // fp_actual = zs::OsFopen(actual, "r");
  // CHECK(fp_actual != NULL);

  // fp_expected = zs::OsFopen(expected, "r");
  // CHECK(fp_expected != NULL);

  // while (1) {
  //   int n_actual = fread(buf_actual, 1, 1024, fp_actual);
  //   int n_expected = fread(buf_expected, 1, 1024, fp_expected);
  //   if (n_actual == 0 && n_expected == 0) {
  //     break;
  //   }

  //   CHECK_ARRAY_EQUAL(buf_actual, buf_expected, 1024);
  // }

  // return true;
}

}  // namespace zs
