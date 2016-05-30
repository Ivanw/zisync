#include <UnitTest++/UnitTest++.h>
#include <string>

#include "zisync/kernel/platform/platform.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/zslog.h"

using zs::GenConflictFilePath;
using std::string;

static int CreateFile(const string &path) {
  string dir_name = zs::OsDirName(path);
  zs::OsCreateDirectory(dir_name, false);
  FILE *fp = fopen(path.c_str(), "w");
  if (fp == NULL) {
    printf("%s\n",zs::OsGetLastErr());
    assert(false);
    return -1;
  }
  fclose(fp);
  return 0;
}

TEST(GenConflictFilePath) {
  string pwd;
  zs::OsGetFullPath(".", &pwd);
  zs::OsCreateDirectory(pwd + "/Test", true);
  string tree_root = pwd + "/Test";
  string conflict_file_path;

  string file_path = "/file";
  string expected_conflict_file_path = "/file.conflict";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  // has exptected file
  CreateFile(tree_root + expected_conflict_file_path);
  expected_conflict_file_path = "/file.conflict.1";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  
  file_path = "/dir/dir/file";
  expected_conflict_file_path = "/dir/dir/file.conflict";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  // has exptected file
  CreateFile(tree_root + expected_conflict_file_path);
  expected_conflict_file_path = "/dir/dir/file.conflict.1";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  
  file_path = "/file.text";
  expected_conflict_file_path = "/file.conflict.text";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  // has exptected file
  CreateFile(tree_root + expected_conflict_file_path);
  expected_conflict_file_path = "/file.conflict.1.text";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  
  file_path = "/dir/dir/file.text";
  expected_conflict_file_path = "/dir/dir/file.conflict.text";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  // has exptected file
  CreateFile(tree_root + expected_conflict_file_path);
  expected_conflict_file_path = "/dir/dir/file.conflict.1.text";
  zs::GenConflictFilePath(&conflict_file_path, tree_root, file_path);
  CHECK_EQUAL(expected_conflict_file_path, conflict_file_path);
  
  zs::OsDeleteDirectories(tree_root, true);
}

TEST(GenFixedStringForDatabase) {
  string path = "/home/super's test";
  string expected_path = "/home/super''s test";

  CHECK_EQUAL(expected_path, zs::GenFixedStringForDatabase(path));
}
