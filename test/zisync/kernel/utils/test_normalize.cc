// Copyright 2014, zisync.com

#include <UnitTest++/UnitTest++.h>

#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/normalize_path.h"

using std::string;
using zs::NormalizePath;
using zs::IsAbsolutePath;

TEST(NormalizePath_Linux) {
  string path = "///";
  string expected_path = "/";
  int ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/test/test/../test";
  expected_path = "/test/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/test/";
  expected_path = "/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/";
  expected_path = "/";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
}

TEST(NormalizePath_Windows) {
#ifdef ZS_TEST
  zs::Config::set_test_platform(zs::PLATFORM_WINDOWS);

  string path = "E:/";
  string expected_path = "E:/";
  int ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "E:///test";
  expected_path = "E:/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "E:/test/test/../test";
  expected_path = "E:/test/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "E:/test/";
  expected_path = "E:/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "///";
  expected_path = "/";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/test/test/../test";
  expected_path = "/test/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/test/";
  expected_path = "/test";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);
  
  path = "/";
  expected_path = "/";
  ret = NormalizePath(&path);
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(expected_path, path);

  zs::Config::clear_test_platform();
#endif
}

TEST(IsAbsolutePath_Linux) {
  string path = "/";
  CHECK_EQUAL(true, IsAbsolutePath(path));
  
  path = "///";
  CHECK_EQUAL(true, IsAbsolutePath(path));
  
  path = "test/";
  CHECK_EQUAL(false, IsAbsolutePath(path));
  
  path = "";
  CHECK_EQUAL(false, IsAbsolutePath(path));
}

TEST(IsAbsolutePath_Windows) {
#ifdef ZS_TEST
  zs::Config::set_test_platform(zs::PLATFORM_WINDOWS);
  string path = "E:/";
  CHECK_EQUAL(true, IsAbsolutePath(path));
  
  path = "E:";
  CHECK_EQUAL(false, IsAbsolutePath(path));
  
  path = "E";
  CHECK_EQUAL(false, IsAbsolutePath(path));
  
  path = "";
  CHECK_EQUAL(false, IsAbsolutePath(path));
  
  path = "/E:/";
  CHECK_EQUAL(false, IsAbsolutePath(path));
  
  path = "E:///test";
  CHECK_EQUAL(true, IsAbsolutePath(path));
  
  zs::Config::clear_test_platform();
#endif
}
