#include "zisync/kernel/platform/platform.h"

#include <UnitTest++/UnitTest++.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/sync_list.h"
#include <string>
#include <vector>
#include <iostream>
#include "zisync/kernel/zslog.h"

using zs::IZiSyncKernel;
using zs::SyncInfo;
using zs::TreeInfo;
using zs::SyncList;
using zs::WhiteSyncList;
using zs::DefaultLogger;
using zs::err_t;

static IZiSyncKernel* kernel;

struct SomeFixture {
  SomeFixture() {
    err_t zisync_ret = zs::ZISYNC_SUCCESS;
    std::string app_path;
    int ret = zs::OsGetFullPath(".", &app_path);
    CHECK_EQUAL(0, ret);

    kernel = zs::GetZiSyncKernel("actual");
    CHECK(kernel != NULL);

    zisync_ret = kernel->Initialize(
        app_path.c_str(), "test", "test", app_path.c_str());
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

    zisync_ret = kernel->Startup(app_path.c_str(), 8849, NULL);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  
    zisync_ret = zs::ZISYNC_SUCCESS;
    SyncInfo sync_info;
    zisync_ret = kernel->CreateSync("test_sync", &sync_info);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

    const std::string path = app_path + "/test";
    ret = zs::OsCreateDirectory(path, false);
    CHECK_EQUAL(0, ret);
    TreeInfo tree_info;
    zisync_ret = kernel->CreateTree(
        sync_info.sync_id, path.c_str(), &tree_info);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

    tree_id = tree_info.tree_id;
  }

  ~SomeFixture() {
    kernel->Shutdown();
  }

  int32_t tree_id;
};

TEST_FIXTURE(SomeFixture, WhiteSyncList_NeedSync) {
  std::vector<std::string> paths;
  err_t zisync_ret = SyncList::Insert(tree_id, "/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK(SyncList::NeedSync(tree_id, "/test/test") == true);
  CHECK(SyncList::NeedSync(tree_id, "/test") == true);
  CHECK(SyncList::NeedSync(tree_id, "/test/test/file") == true);
  CHECK(SyncList::NeedSync(tree_id, "/test/test/test/file") == true);
  CHECK(SyncList::NeedSync(tree_id, "/test2") == false);
  CHECK(SyncList::NeedSync(tree_id, "/test/tes") == false);
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case1) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_EXIST, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }

  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case2) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  expected_paths.push_back("/home/test123");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test123");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test123"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case3) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  
  std::vector<std::string> paths;
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case4) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  expected_paths.push_back("/home/test123");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test123");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test123"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case5) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test/dir");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/dir"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case6) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test/dir");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test/dir"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case7) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_EXIST, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_EXIST, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case8) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/super"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case9) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/ho/test");
  expected_paths.push_back("/home/test");
  expected_paths.push_back("/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/ho/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/ho/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case10) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/never");
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/never");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/never");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_EXIST, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/never"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case11) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/never");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/never"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case12) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/never");
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/never");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/never/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_EXIST, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/never"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/never/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case13) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/super/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/super/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/super/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD,
              SyncList::GetSyncListPathType(tree_id, "/home/super/super"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Insert_Case14) {
  std::vector<std::string> expected_paths;
  expected_paths.push_back("/home/never");
  expected_paths.push_back("/home/super");
  expected_paths.push_back("/home/test");
  
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/never");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  std::vector<std::string> list_paths;
  zisync_ret = SyncList::List(tree_id, &list_paths);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expected_paths.size(), list_paths.size());
  for (unsigned int i = 0; i < expected_paths.size(); i ++) {
    CHECK_EQUAL(expected_paths[i], list_paths[i]);
  }
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/super"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF,
              SyncList::GetSyncListPathType(tree_id, "/home/never"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_GetSyncListPathType_Case1) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/test/test2");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/test/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/test/test2"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/test/test3"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_PARENT, 
              SyncList::GetSyncListPathType(tree_id, "/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD, 
              SyncList::GetSyncListPathType(tree_id, "/test/test/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/abc"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_GetSyncListPathType_Case2) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD, 
              SyncList::GetSyncListPathType(tree_id, "/test/test2"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD, 
              SyncList::GetSyncListPathType(tree_id, "/abc"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_GetSyncListPathType_Case3) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_PARENT, 
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_CHILD, 
              SyncList::GetSyncListPathType(tree_id, "/test/abc"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case1) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "home/test/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_NOENT, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  
  zisync_ret = SyncList::Remove(tree_id, "/home");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_NOENT, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  
  zisync_ret = SyncList::Remove(tree_id, "/home/test123");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_NOENT, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  
  zisync_ret = SyncList::Remove(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case2) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_NOENT, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/"));
  
  zisync_ret = SyncList::Remove(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/home"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case3) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
  
  zisync_ret = SyncList::Remove(tree_id, "/");
  CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_LIST_NOENT, zisync_ret);
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case4) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/test/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "/home/test/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_PARENT, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/home/test/super"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/home/test/test"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case5) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_STRANGER, 
              SyncList::GetSyncListPathType(tree_id, "/home/test"));
  CHECK_EQUAL(zs::SYNC_LIST_PATH_TYPE_SELF, 
              SyncList::GetSyncListPathType(tree_id, "/home/super"));
}

TEST_FIXTURE(SomeFixture, WhiteSyncList_Remove_Case6) {
  err_t zisync_ret = SyncList::Insert(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Insert(tree_id, "/home/sudo");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = SyncList::Remove(tree_id, "/home/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Remove(tree_id, "/home/super");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = SyncList::Remove(tree_id, "/home/sudo");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
}
