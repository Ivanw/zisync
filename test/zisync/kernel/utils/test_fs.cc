// Copyright 2014, zisync.com
#include <UnitTest++/UnitTest++.h>  // NOLINT

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"

using std::string;

static int CreateFile(const string &path) {
  FILE *fp = fopen(path.c_str(), "w");
  if (fp == NULL) {
    assert(false);
    return -1;
  }
  fclose(fp);
  return 0;
}

static char ROOT[] = "./Test";

SUITE(FileSystem) {
  class SomeFixture {
   public:
    SomeFixture() {
      zs::OsDeleteDirectories("./Test");
      zs::OsCreateDirectory("./Test", true);
    }
    ~SomeFixture() {
      zs::OsDeleteDirectories("./Test");
    }
  };

  TEST_FIXTURE(SomeFixture, DeleteFile) {
    string path = ROOT;
    path.append("/file");
    int ret;
    ret = zs::OsDeleteFile(path, false);
    CHECK_EQUAL(0, ret);
    CreateFile(path);
    CHECK_EQUAL(true, zs::OsFileExists(path));
    ret = zs::OsDeleteFile(path, false);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsFileExists(path));
  }

  TEST_FIXTURE(SomeFixture, CreateDirectory) {
    string path = ROOT;
    path.append("/dir");
    int ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(true, zs::OsDirExists(path));
    /* exist */
    ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);

    path = ROOT;
    path.append("/dir/dir/dir");
    ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(true, zs::OsDirExists(path));
  }

  TEST_FIXTURE(SomeFixture, CreateDirectoryDeleteFileExisting) {
    string path = ROOT;
    path.append("/dir");
    
    CreateFile(path);
    CHECK_EQUAL(true, zs::OsFileExists(path));
    int ret = zs::OsCreateDirectory(path, false);
    CHECK_EQUAL(-1, ret);
    ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(true, zs::OsDirExists(path));

	path.append("/dir");
	CreateFile(path);
	CHECK_EQUAL(true, zs::OsFileExists(path));
	path.append("/dir/dir/dir/dir");
	ret = zs::OsCreateDirectory(path, false);
	CHECK_EQUAL(-1, ret);
	ret = zs::OsCreateDirectory(path, true);
	CHECK_EQUAL(0, ret);
	CHECK_EQUAL(true, zs::OsDirExists(path));
  }

  TEST_FIXTURE(SomeFixture, Exists) {
    string path = ROOT;
    path.append("/file");
    
    CreateFile(path);
    CHECK_EQUAL(true, zs::OsExists(path));
    CHECK_EQUAL(true, zs::OsFileExists(path.c_str()));
    CHECK_EQUAL(false, zs::OsDirExists(path.c_str()));
    
    path = ROOT;
    path.append("/dir");
    
    zs::OsCreateDirectory(path, false);
    CHECK_EQUAL(true, zs::OsExists(path));
    CHECK_EQUAL(false, zs::OsFileExists(path.c_str()));
    CHECK_EQUAL(true, zs::OsDirExists(path.c_str()));
  }

  TEST_FIXTURE(SomeFixture, DeleteDirectory) {
    string path = ROOT;
    path.append("/dir");
    int ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(true, zs::OsDirExists(path));
    ret = zs::OsDeleteDirectory(path);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsDirExists(path));
    
    path = ROOT;
    path.append("/dir/dir/dir");
    ret = zs::OsCreateDirectory(path, true);
    CHECK_EQUAL(0, ret);
    path = ROOT;
    path.append("/dir");
    CHECK_EQUAL(true, zs::OsDirExists(path));
    ret = zs::OsDeleteDirectory(path);
    CHECK_EQUAL(-1, ret);
    CHECK_EQUAL(true, zs::OsDirExists(path));
    ret = zs::OsDeleteDirectories(path);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsDirExists(path));
  }

  TEST_FIXTURE(SomeFixture, Rename) {
    /* test is_overwrite  */
    string src = ROOT;
    src.append("/file1");
    
    string dst = ROOT;
    dst.append("/file2");

    CreateFile(src);
    int ret = zs::OsRename(src, dst);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsFileExists(src));
    CHECK_EQUAL(true, zs::OsFileExists(dst));
    
    CreateFile(src);
    ret = zs::OsRename(src, dst, false);
    CHECK_EQUAL(-1, ret);
    CHECK_EQUAL(true, zs::OsFileExists(src));
    ret = zs::OsRename(src, dst);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsFileExists(src));
    CHECK_EQUAL(true, zs::OsFileExists(dst));

    /*  test dst parent dir not exists */
    src = ROOT;
    src.append("/file2");
    string dir = ROOT;
    dir.append("/dir");
    dst = ROOT;
    dst.append("/dir/dir/file2");
    CHECK_EQUAL(false, zs::OsDirExists(dir));
    ret = zs::OsRename(src, dst);
    CHECK_EQUAL(0, ret);
    CHECK_EQUAL(false, zs::OsFileExists(src));
    CHECK_EQUAL(true, zs::OsFileExists(dst));
  }
}
