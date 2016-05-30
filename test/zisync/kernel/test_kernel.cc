// // Copyright 2014, zisync.com
// 
#include <memory>
#include <cassert>
#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <UnitTest++/TestReporterStdout.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/permission.h"
#include "zisync/kernel/proto/verify.pb.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"

using zs::err_t;
using zs::IZiSyncKernel;
using zs::ZISYNC_SUCCESS;
using zs::ZISYNC_ERROR_ADDRINUSE;
using zs::ZISYNC_ERROR_INVALID_PORT;
using zs::ZISYNC_ERROR_SYNC_NOENT;
using zs::ZISYNC_ERROR_SYNC_EXIST;
using zs::ZISYNC_ERROR_TREE_NOENT;
using zs::ZISYNC_ERROR_TREE_EXIST;
using zs::ZISYNC_ERROR_DIR_NOENT;
using zs::ZISYNC_ERROR_NOT_STARTUP;
using zs::ZISYNC_ERROR_NESTED_TREE;
using zs::ZISYNC_ERROR_INVALID_PATH;
using std::unique_ptr;
using zs::SyncInfo;
using zs::BackupInfo;
using zs::TreeInfo;
using zs::Config;
using zs::OsTcpSocket;
using zs::Licences;
using zs::OsUdpSocket;
using zs::DefaultLogger;
using zs::FindRequest;
using zs::MsgStat;
using zs::ZSLOG_ERROR;
using zs::ZSLOG_INFO;
using std::string;
using zs::StringFormat;
using zs::OsGetFullPath;
using zs::IContentProvider;
using zs::GetContentResolver;
using zs::IContentResolver;
using zs::TableLicences;
using zs::ContentValues;
using zs::ZISYNC_ERROR_CONTENT;
using zs::AOC_REPLACE;


static IZiSyncKernel *kernel;
static DefaultLogger logger("./Log");
std::string app_path;
std::string backup_root;
std::string test_root;

static int64_t last_time = -1;
static std::string key = "C4GVMA3B5S8HLOPLUKCE24K0M7MOP1";

static inline void PrintTime(const char *prefix) {
  if (last_time != -1 && prefix != NULL) {
    ZSLOG_ERROR("%s : %" PRId64, prefix, zs::OsTimeInMs() - last_time);
  }
  last_time = zs::OsTimeInMs();
}


struct SomeFixture {
  SomeFixture() {
    err_t zisync_ret;

    PrintTime(NULL);
#ifdef ZS_TEST
    Config::disable_default_perms();
#endif
    
    int ret = zs::OsCreateDirectory(test_root, true);
    assert(ret == 0);
    ret = zs::OsCreateDirectory(backup_root, true);
    assert(ret == 0);
    zisync_ret = kernel->Initialize(
        app_path.c_str(), "test_kernel", "test", backup_root.c_str());
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
    assert(zisync_ret == ZISYNC_SUCCESS);
    // for user permission
//    std::string mac_address = "test";
    zs::GetLicences()->SavePermKey(key);
//    zs::GetLicences()->SaveMacAddress(mac_address);
    std::map<Http::PrivilegeCode, std::string> perms = {
      {Http::CreateFolder, "-1"},
      {Http::ShareSwitch, "-1"},
      {Http::ShareReadWrite, "-1"},
      {Http::ShareRead, "-1"},
      {Http::ShareWrite, "-1"},
      {Http::DeviceSwitch, "-1"},
      {Http::DeviceEdit, "-1"},
      {Http::OnlineSwitch, "-1"},
      {Http::OnlineOpen, "-1"},
      {Http::OnlineDownload, "-1"},
      {Http::OnlineUpload, "-1"},
      {Http::TransferSwitch, "-1"},
      {Http::HistorySwitch, "-1"},
      {Http::ChangeSharePermission, "-1"},
      {Http::RemoveShareDevice, "-1"},
      {Http::CreateBackup, "-1"},
    };

    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);
    response.set_role("Premium");
    response.set_expiredofflinetime(-1);
    response.set_lastcontacttime(-1);
    response.set_createdtime(-1);
    for (auto it = perms.begin(); it != perms.end(); it++) {
     Http::Permission *perm = response.add_permissions();
     assert(perm != NULL);
     perm->set_privilege(it->first);
     perm->set_constraint(it->second);
    }
    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    PrintTime("Prepare");
  }
  ~SomeFixture() {
    PrintTime("OnTest");
    zs::GetLicences()->CleanUp();
    kernel->Shutdown();
    PrintTime("Shutdown");
    int ret = zs::OsDeleteDirectories(test_root);
    assert(ret == 0);
    ret = zs::OsDeleteDirectories(backup_root);
    assert(ret == 0);
    
#ifdef ZS_TEST
    zs::Config::enable_default_perms();
#endif
  }
};

void ClearConfig() {
  Config::set_data_port(0);
  Config::set_route_port(0);
  Config::set_device_name("");
  Config::set_device_uuid("");
  Config::set_account_name("");
  Config::set_discover_port(0);
  Config::set_sync_interval(0);
  Config::set_backup_root("");
}


#define _CHECK_EQUAL(expected, actual) \
{ \
  CHECK_EQUAL(expected, actual); \
  if (expected != actual) { \
    return false; \
  } \
}

static inline err_t QuerySyncInfo(zs::QuerySyncInfoResult *result) {
  sleep(1);
  return kernel->QuerySyncInfo(result);
}

static inline err_t QuerySyncInfo(
    int32_t sync_id, SyncInfo *info) {
  sleep(1);
  return kernel->QuerySyncInfo(sync_id, info);
}

static inline err_t QueryBackupInfo(zs::QueryBackupInfoResult *result) {
  sleep(1);
  return kernel->QueryBackupInfo(result);
}



TEST_FIXTURE(SomeFixture, Initialize) {
  string account = kernel->GetAccountName();
  CHECK_EQUAL("test_kernel", account);
  string backup_root_ = kernel->GetBackupRoot();
  CHECK_EQUAL(backup_root, backup_root_);
}

TEST(InvalidPath_Initialize) {
  err_t zisync_ret;

  zisync_ret = kernel->Initialize(
      "test", "test", "test", backup_root.c_str());
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
  
  zisync_ret = kernel->Initialize(
      app_path.c_str(), "test", "test", "test");
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
}

TEST(InvalidPath_Startup) {
  err_t zisync_ret;

  zisync_ret = kernel->Startup("test", 8848, NULL);
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);

  kernel->Shutdown();
}

TEST_FIXTURE(SomeFixture, InvalidPath_CreateTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  TreeInfo tree_info;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);

  zisync_ret = kernel->CreateTree(sync_info.sync_id, "test", &tree_info);
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
}

TEST_FIXTURE(SomeFixture, InvalidPath_Favorite) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "test");
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "test");
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
}

TEST(NormalizePath_Initialize) {
  err_t zisync_ret;
  int ret = zs::OsCreateDirectory(backup_root, true);
  assert(ret == 0);

  zisync_ret = kernel->Initialize(
      (app_path + "/").c_str(), "test", "test", 
      (backup_root + "/").c_str());
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
}

TEST(NormalizePath_Startup) {
  err_t zisync_ret;

  zisync_ret = kernel->Startup((app_path + "/").c_str(), 8848, NULL);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  kernel->Shutdown();
}

void CheckTreeInfoEqual(const TreeInfo &info1, const TreeInfo &info2) {
  CHECK_EQUAL(info1.tree_id, info2.tree_id);
  CHECK_EQUAL(info1.tree_root, info2.tree_root);
  CHECK_EQUAL(info1.tree_uuid, info2.tree_uuid);
  CHECK_EQUAL(info1.is_local, info2.is_local);
  CHECK_EQUAL(info1.is_sync_enabled, info2.is_sync_enabled);
  CHECK_EQUAL(info1.device.device_name, info2.device.device_name);
  CHECK_EQUAL(info1.device.device_id, info2.device.device_id);
  CHECK_EQUAL(info1.device.device_type, info2.device.device_type);
  CHECK_EQUAL(info1.device.is_mine, info2.device.is_mine);
  CHECK_EQUAL(info1.device.is_backup, info2.device.is_backup);
  CHECK_EQUAL(info1.device.is_online, info2.device.is_online);
  CHECK_EQUAL(info1.device.backup_root, info2.device.backup_root);
}

static inline bool IsTreeDBFileExisting(const char *tree_uuid) {
  std::string tree_db_path = Config::database_dir();
  zs::StringFormat(&tree_db_path,
                   "%s/%s.db", Config::database_dir().c_str(),
                   tree_uuid);
  return zs::OsFileExists(tree_db_path);
}

TEST_FIXTURE(SomeFixture, NormalizePath_CreateTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  TreeInfo tree_info;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);

  zisync_ret = kernel->CreateTree(
      sync_info.sync_id, (path + "/").c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  CHECK_EQUAL(true, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
  zs::QuerySyncInfoResult result;
  zisync_ret = QuerySyncInfo(&result);
  assert(1 == static_cast<int>(result.sync_infos.size()));
  assert(1 == static_cast<int>(result.sync_infos[0].trees.size()));
  CheckTreeInfoEqual(tree_info, result.sync_infos[0].trees[0]);
}

TEST_FIXTURE(SomeFixture, NormalizePath_Favorite) {
  err_t zisync_ret;
  int ret;

  string pwd = test_root;
  string root = pwd + "/test";
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test/");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test/"));
  
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test/");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test/"));
}

TEST_FIXTURE(SomeFixture, test_GetSet) {
  char device_name_[] = "test";
  char user_name_[] = "user_test";
  char user_passwd_[] = "user_passwd";
  string backup_root_ = test_root; 
  backup_root_ += "new_backup_root";
  // int16_t discover_port_ = 1234;
  int32_t new_router_port = 1235, new_data_port = 1237, 
          new_discover_port = 1235;
  err_t ret;
  int32_t router_port, data_port, discover_port;
  int32_t old_router_port, old_data_port, old_discover_port;
  int socket_ret;

  /*  Set after startup */
  
  ret = kernel->SetDeviceName(device_name_);
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  std::string device_name = kernel->GetDeviceName();
  CHECK_EQUAL(device_name_, device_name);
  ret = kernel->SetAccount(user_name_, user_passwd_);
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  std::string user_name = kernel->GetAccountName();
  CHECK_EQUAL(user_name_, user_name);
  ret = kernel->SetBackupRoot(backup_root_.c_str());
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  std::string new_backup_root = kernel->GetBackupRoot();
  CHECK_EQUAL(backup_root_, new_backup_root);

  old_router_port = kernel->GetRoutePort();
  ret = kernel->SetRoutePort(new_router_port);
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  router_port = kernel->GetRoutePort();
  CHECK_EQUAL(new_router_port, router_port);
  /* check old port has been released */
  int loop = 0;
  string uri;
  printf("Loop wait: ");
  fflush(stdout);
  do {
    sleep(1);
    StringFormat(&uri, "tcp://*:%" PRId32, old_router_port);
    OsTcpSocket socket2(uri);
    socket_ret = socket2.Bind();
    loop ++;
    printf("%d, ", loop);
    fflush(stdout);
  } while ( socket_ret != 0 && loop < 100);
  printf("\n");
  CHECK_EQUAL(0, socket_ret);
  /* check new port has been occupied */
  StringFormat(&uri, "tcp://*:%" PRId32, new_router_port);
  OsTcpSocket socket3(uri);
  socket_ret = socket3.Bind();
  CHECK_EQUAL(EADDRINUSE, socket_ret);

  old_discover_port = kernel->GetDiscoverPort();
  ret = kernel->SetDiscoverPort(new_discover_port);
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  discover_port = kernel->GetDiscoverPort();
  CHECK_EQUAL(new_discover_port, discover_port);
  /* check old port has been released */
  StringFormat(&uri, "udp://*:%" PRId32, old_discover_port);
  OsUdpSocket socket4(uri);
  socket_ret = socket4.Bind();
  CHECK_EQUAL(0, socket_ret);
  /* check new port has been occupied */
  StringFormat(&uri, "udp://*:%" PRId32, new_discover_port);
  OsUdpSocket socket5(uri);
  socket_ret = socket5.Bind();
  CHECK_EQUAL(EADDRINUSE, socket_ret);

  old_data_port = kernel->GetDataPort();
  ret = kernel->SetDataPort(new_data_port);
  CHECK_EQUAL(ZISYNC_SUCCESS, ret);
  data_port = kernel->GetDataPort();
  CHECK_EQUAL(new_data_port, data_port);
  /* check old port has been released */
  StringFormat(&uri, "tcp://*:%" PRId32, old_data_port);
  OsTcpSocket socket6(uri);
  socket_ret = socket6.Bind();
  CHECK_EQUAL(0, socket_ret);
  /* check new port has been occupied */
  StringFormat(&uri, "tcp://*:%" PRId32, new_data_port);
  OsTcpSocket socket7(uri);
  socket_ret = socket7.Bind();
  CHECK_EQUAL(EADDRINUSE, socket_ret);

  // ReadFromContent()
  ClearConfig();
  ret = Config::ReadFromContent();
  CHECK_EQUAL(ret, ZISYNC_SUCCESS);
  router_port = kernel->GetRoutePort();
  CHECK_EQUAL(new_router_port, router_port);
  discover_port = kernel->GetDiscoverPort();
  CHECK_EQUAL(new_discover_port, discover_port);
  data_port = kernel->GetDataPort();
  CHECK_EQUAL(new_data_port, data_port);
  device_name = kernel->GetDeviceName();
  CHECK_EQUAL(device_name_, device_name);
  user_name = kernel->GetAccountName();
  CHECK_EQUAL(user_name_, user_name);
  new_backup_root = kernel->GetBackupRoot();
  CHECK_EQUAL(backup_root_, new_backup_root);
}

TEST(NOT_STARTUP) {
  char device_name_[] = "test";
  char user_name_[] = "user_test";
  char user_passwd_[] = "user_passwd";
  // int16_t discover_port_ = 1234;
  int32_t new_router_port = 1235, new_data_port = 1237, 
          new_discover_port = 1235;

  err_t ret = kernel->SetDeviceName(device_name_);
  CHECK_EQUAL(ZISYNC_ERROR_NOT_STARTUP, ret);
  ret = kernel->SetAccount(user_name_, user_passwd_);
  CHECK_EQUAL(ZISYNC_ERROR_NOT_STARTUP, ret);

  ret = kernel->SetRoutePort(new_router_port);
  CHECK_EQUAL(ZISYNC_ERROR_NOT_STARTUP, ret);
  ret = kernel->SetDataPort(new_data_port);
  CHECK_EQUAL(ZISYNC_ERROR_NOT_STARTUP, ret);
  ret = kernel->SetDiscoverPort(new_discover_port);
  CHECK_EQUAL(ZISYNC_ERROR_NOT_STARTUP, ret);
}

TEST_FIXTURE(SomeFixture, test_ADDINUSE) {
  kernel->Shutdown();
  err_t zisync_ret;

  /* occupy port then set before startup */
  OsTcpSocket socket1("tcp://*:8847");
  OsUdpSocket socket2("udp://*:8847");
  int ret = socket1.Bind();
  assert(ret == 0);
  ret = socket2.Bind();
  assert(ret == 0);

  zisync_ret = kernel->Startup(app_path.c_str(), 8847, NULL);
  CHECK_EQUAL(ZISYNC_ERROR_ADDRINUSE, zisync_ret);
  /* occupy port, then startup, then set */
  zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->SetRoutePort(8847);
  CHECK_EQUAL(ZISYNC_ERROR_ADDRINUSE, zisync_ret);
  zisync_ret = kernel->SetDataPort(8847);
  CHECK_EQUAL(ZISYNC_ERROR_ADDRINUSE, zisync_ret);
  zisync_ret = kernel->SetDiscoverPort(8847);
  CHECK_EQUAL(ZISYNC_ERROR_ADDRINUSE, zisync_ret);
}

TEST_FIXTURE(SomeFixture, test_INVALID_PORT) {
  kernel->Shutdown();
  err_t zisync_ret;

  /* set after startup */
  zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->SetRoutePort(1);
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PORT, zisync_ret);
  zisync_ret = kernel->SetDataPort(1);
  CHECK_EQUAL(ZISYNC_ERROR_INVALID_PORT, zisync_ret);
  // zisync_ret = kernel->SetDiscoverPort(1);
  // CHECK_EQUAL(ZISYNC_ERROR_INVALID_PORT, zisync_ret);

  /* set after startup */
  zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->SetRoutePort(65527);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->SetDataPort(65529);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->SetDiscoverPort(65530);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
}

TEST_FIXTURE(SomeFixture, test_CreateSync) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  CHECK_EQUAL(sync_name, sync_info.sync_name.c_str());
  CHECK_EQUAL(0, static_cast<int>(sync_info.trees.size()));
}

void CheckSyncInfoEqual(const SyncInfo &info1, const SyncInfo &info2,
                        bool compare_id = true) {
  if (compare_id) {
    CHECK_EQUAL(info1.sync_id, info2.sync_id);
  }
  CHECK_EQUAL(info1.sync_name, info2.sync_name);
  CHECK_EQUAL(info1.sync_uuid, info2.sync_uuid);
  CHECK_EQUAL(info1.is_share, info2.is_share);
  CHECK_EQUAL(info1.trees.size(), info2.trees.size());
  for (unsigned int i = 0; i < info1.trees.size(); i ++) {
    CheckTreeInfoEqual(info1.trees[i], info2.trees[i]);
  }
}

TEST_FIXTURE(SomeFixture, QuerySync) {
  err_t zisync_ret;
  zs::QuerySyncInfoResult result;
  zisync_ret = QuerySyncInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));
  const char *sync_name = "test";
  SyncInfo sync_info1;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info1);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  SyncInfo sync_info2;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = QuerySyncInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));
  
  std::string pwd = test_root;
  string path = pwd + "/test1";
  TreeInfo tree_info;
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info1.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  sync_info1.trees.push_back(tree_info);
  path = pwd + "/test2";
  ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info2.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  sync_info2.trees.push_back(tree_info);

  zisync_ret = QuerySyncInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(2, static_cast<int>(result.sync_infos.size()));
  CheckSyncInfoEqual(sync_info1, result.sync_infos[0]);
  CheckSyncInfoEqual(sync_info2, result.sync_infos[1]);

  SyncInfo sync_info;
  zisync_ret = QuerySyncInfo(sync_info1.sync_id, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CheckSyncInfoEqual(sync_info1, sync_info);
  zisync_ret = QuerySyncInfo(sync_info1.sync_id + 100, &sync_info);
  CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  zisync_ret = QuerySyncInfo(sync_info2.sync_id, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CheckSyncInfoEqual(sync_info2, sync_info);
}

// TEST_FIXTURE(SomeFixture, DestroySync) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info1;
//   zisync_ret = kernel->CreateSync(sync_name, &sync_info1);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   SyncInfo sync_info2;
//   zisync_ret = kernel->CreateSync(sync_name, &sync_info2);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
//   std::string pwd = test_root;
//   string path = pwd + "/test1";
//   TreeInfo tree_info;
//   int ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info1.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   sync_info1.trees.push_back(tree_info);
//   path = pwd + "/test2";
//   ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info2.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   sync_info2.trees.push_back(tree_info);

//   zisync_ret = kernel->DestroySync(sync_info1.sync_id);
//   zs::QuerySyncInfoResult result;
//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(1, static_cast<int>(result.sync_infos.size()));
//   CheckSyncInfoEqual(sync_info2, result.sync_infos[0]);

//   zisync_ret = kernel->DestroySync(sync_info1.sync_id);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
//   zisync_ret = kernel->DestroySync(sync_info2.sync_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));
// }

// TEST_FIXTURE(SomeFixture, test_ImportExportSync) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info1;
//   zisync_ret = kernel->CreateSync(sync_name, &sync_info1);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
// 
//   std::string blob;
//   /*  Export with noent */
//   zisync_ret = kernel->ExportSync(sync_info1.sync_id + 1, &blob);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
// 
//   zisync_ret = kernel->ExportSync(sync_info1.sync_id, &blob);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   /* Exist */
//   SyncInfo sync_info2;
//   zisync_ret = kernel->ImportSync(blob, &sync_info2);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_EXIST, zisync_ret);
// 
//   zisync_ret = kernel->DestroySync(sync_info1.sync_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   zs::QuerySyncInfoResult result;
//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));
//   zisync_ret = kernel->ExportSync(sync_info1.sync_id, &blob);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
// 
//   zisync_ret = kernel->ImportSync(blob, &sync_info2);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CheckSyncInfoEqual(sync_info1, sync_info2, false);
// 
//   /* Exist */
//   zisync_ret = kernel->ImportSync(blob, &sync_info2);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_EXIST, zisync_ret);
// }
// 
// TEST_FIXTURE(SomeFixture, ImportDestroyedSync) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info1, sync_info2;
//   std::string blob;
//   zisync_ret = kernel->CreateSync(sync_name, &sync_info1);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   
//   zisync_ret = kernel->ExportSync(sync_info1.sync_id, &blob);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
// 
//   zisync_ret = kernel->DestroySync(sync_info1.sync_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   
//   zisync_ret = kernel->ImportSync(blob, &sync_info2);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CheckSyncInfoEqual(sync_info1, sync_info2, false);
//   
//   std::string pwd;
//   int ret = OsGetFullPath(".", &pwd);
//   assert(ret == 0);
//   string path = pwd + "/test1";
//   TreeInfo tree_info;
//   ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info1.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   sync_info1.trees.push_back(tree_info);
//   
//   zs::QuerySyncInfoResult result;
//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(1, static_cast<int>(result.sync_infos.size()));
//   CheckSyncInfoEqual(sync_info1, result.sync_infos[0]);
// }

TEST_FIXTURE(SomeFixture, test_CreateTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  TreeInfo tree_info;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";

  std::string noent_path;
  StringFormat(&noent_path, "%snoent", path.c_str());

  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id + 1, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, noent_path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_ERROR_DIR_NOENT, zisync_ret);

  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  CHECK_EQUAL(true, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
  zs::QuerySyncInfoResult result;
  zisync_ret = QuerySyncInfo(&result);
  assert(1 == static_cast<int>(result.sync_infos[0].trees.size()));
  CheckTreeInfoEqual(tree_info, result.sync_infos[0].trees[0]);

  TreeInfo query_tree_info;
  zisync_ret = kernel->QueryTreeInfo(tree_info.tree_id, &query_tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CheckTreeInfoEqual(tree_info, query_tree_info);
  
  /* TREE exist */
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_ERROR_TREE_EXIST, zisync_ret);

}

// TEST_FIXTURE(SomeFixture, SetTreeRoot) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info;
//   TreeInfo tree_info;

//   zisync_ret = kernel->CreateSync(sync_name, &sync_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   std::string path = test_root;
//   path += "/test";

//   std::string noent_path;
//   StringFormat(&noent_path, "%snoent", path.c_str());

//   int ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
//   zisync_ret = kernel->SetTreeRoot(tree_info.tree_id, "another");
//   CHECK_EQUAL(ZISYNC_ERROR_INVALID_PATH, zisync_ret);
//   string new_tree_root = path + "/set_tree_root_test";
//   zisync_ret = kernel->SetTreeRoot(tree_info.tree_id, new_tree_root.c_str());
//   CHECK_EQUAL(ZISYNC_ERROR_DIR_NOENT, zisync_ret);
//   ret = zs::OsCreateDirectory(new_tree_root, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->SetTreeRoot(tree_info.tree_id, new_tree_root.c_str());
//   CHECK_EQUAL(ZISYNC_ERROR_NESTED_TREE, zisync_ret);
//   new_tree_root = path + "another";
//   ret = zs::OsCreateDirectory(new_tree_root, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->SetTreeRoot(tree_info.tree_id, new_tree_root.c_str());
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
//   TreeInfo query_tree_info;
//   zisync_ret = kernel->QueryTreeInfo(tree_info.tree_id, &query_tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(new_tree_root, query_tree_info.tree_root);
// }

// TEST_FIXTURE(SomeFixture, DestroyTreeTest) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info;
//   TreeInfo tree_info;

//   zisync_ret = kernel->CreateSync(sync_name, &sync_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   std::string path = test_root;
//   path += "/test";

//   int ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   zisync_ret = kernel->DestroyTree(tree_info.tree_id + 1);
//   CHECK_EQUAL(ZISYNC_ERROR_TREE_NOENT, zisync_ret);
//   zisync_ret = kernel->DestroyTree(tree_info.tree_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
//   zs::QuerySyncInfoResult result;
//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));
  
//   string old_tree_uuid = tree_info.tree_uuid;
//   zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(true, old_tree_uuid != tree_info.tree_uuid);
// }

TEST_FIXTURE(SomeFixture, NestedTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info, sync_info2;
  TreeInfo tree_info, tree_info2;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->CreateSync("test2", &sync_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";

  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  std::string child_path;
  StringFormat(&child_path, "%s/%s", path.c_str(), "test");
  ret = zs::OsCreateDirectory(child_path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, child_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_ERROR_NESTED_TREE, zisync_ret);
  zisync_ret = kernel->CreateTree(sync_info2.sync_id, child_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->DestroyTree(tree_info2.tree_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  size_t pos = path.find_last_of('/');
  std::string parent_path = path.substr(0, pos);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, parent_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_ERROR_NESTED_TREE, zisync_ret);
  zisync_ret = kernel->CreateTree(sync_info2.sync_id, parent_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->DestroyTree(tree_info2.tree_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = kernel->DestroyTree(tree_info.tree_id);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, child_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->DestroyTree(tree_info2.tree_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, parent_path.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
}

TEST_FIXTURE(SomeFixture, CreateDestroyedTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  TreeInfo tree_info;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";

  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = kernel->DestroyTree(tree_info.tree_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  CHECK_EQUAL(true, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
  zs::QuerySyncInfoResult result;
  zisync_ret = QuerySyncInfo(&result);
  assert(1 == static_cast<int>(result.sync_infos[0].trees.size()));
  CheckTreeInfoEqual(tree_info, result.sync_infos[0].trees[0]);
}

TEST_FIXTURE(SomeFixture, QuerySyncWithTree) {
  err_t zisync_ret;
  const char *sync_name = "test";
  SyncInfo sync_info;
  TreeInfo tree_info, tree_info2, tree_info3;

  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  std::string path = test_root;
  path += "/test";

  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  std::string path2 = test_root;
  path2 += "/test2";

  ret = zs::OsCreateDirectory(path2, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path2.c_str(), &tree_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  SyncInfo sync_info2;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  std::string path3 = test_root;
  path2 += "/test3";

  ret = zs::OsCreateDirectory(path3, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(sync_info2.sync_id, path3.c_str(), &tree_info3);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zs::QuerySyncInfoResult result;
  zisync_ret = QuerySyncInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(2, static_cast<int>(result.sync_infos.size()));
  CHECK_EQUAL(2, static_cast<int>(result.sync_infos[0].trees.size()));
  CHECK_EQUAL(1, static_cast<int>(result.sync_infos[1].trees.size()));
  sync_info.trees.push_back(tree_info);
  sync_info.trees.push_back(tree_info2);
  sync_info2.trees.push_back(tree_info3);
  CheckSyncInfoEqual(sync_info, result.sync_infos[0]);
  CheckSyncInfoEqual(sync_info2, result.sync_infos[1]);
}

// TEST_FIXTURE(SomeFixture, DestroySyncWithTree) {
//   err_t zisync_ret;
//   const char *sync_name = "test";
//   SyncInfo sync_info;
//   TreeInfo tree_info, tree_info2, tree_info3;

//   zisync_ret = kernel->CreateSync(sync_name, &sync_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   std::string path = test_root;
//   path += "/test";

//   int ret = zs::OsCreateDirectory(path, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info.sync_id, path.c_str(), &tree_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
 
//   std::string path2 = test_root;
//   path2 += "/test2";
  
//   ret = zs::OsCreateDirectory(path2, true);
//   CHECK_EQUAL(0, ret);
//   zisync_ret = kernel->CreateTree(sync_info.sync_id, path2.c_str(), &tree_info2);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
//   zisync_ret = kernel->DestroySync(sync_info.sync_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   zs::QuerySyncInfoResult result;
//   zisync_ret = QuerySyncInfo(&result);
//   CHECK_EQUAL(0, static_cast<int>(result.sync_infos.size()));

//   CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
//   CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info2.tree_uuid.c_str()));
//   zisync_ret = kernel->DestroyTree(tree_info.tree_id);
//   CHECK_EQUAL(ZISYNC_ERROR_TREE_NOENT, zisync_ret);
//   zisync_ret = kernel->DestroyTree(tree_info2.tree_id);
//   CHECK_EQUAL(ZISYNC_ERROR_TREE_NOENT, zisync_ret);
// }

void CheckBackupInfoEqual(const BackupInfo &info1, const BackupInfo &info2,
                        bool compare_id = true) {
  if (compare_id) {
    CHECK_EQUAL(info1.backup_id, info2.backup_id);
  }
  CHECK_EQUAL(info1.backup_name, info2.backup_name);
  CheckTreeInfoEqual(info1.src_tree, info2.src_tree);
  CHECK_EQUAL(info1.target_trees.size(), info2.target_trees.size());
  for (size_t i = 0; i < info1.target_trees.size(); i ++) {
    CheckTreeInfoEqual(info1.target_trees[i], info2.target_trees[i]);
  }
}

TEST_FIXTURE(SomeFixture, CreateBackup) {
  err_t zisync_ret;
  const char *backup_name = "test";
  BackupInfo backup_info;
  std::string pwd = test_root;
  string path = pwd + "/test1";
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  
  zisync_ret = kernel->CreateBackup(
      backup_name, path.c_str(), &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  CHECK_EQUAL(backup_name, backup_info.backup_name);
  CHECK_EQUAL(path, backup_info.src_tree.tree_root);
  CHECK_EQUAL(0, static_cast<int>(backup_info.target_trees.size()));
  
  BackupInfo exist_backup_info;
  zisync_ret = kernel->CreateBackup(
      backup_name, path.c_str(), &exist_backup_info);
  CHECK_EQUAL(zs::ZISYNC_ERROR_BACKUP_SRC_EXIST, zisync_ret);
  CheckBackupInfoEqual(backup_info, exist_backup_info);
  
  zs::QueryBackupInfoResult result;
  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(result.backups.size()));
  CheckBackupInfoEqual(backup_info, result.backups[0]);
  
  const char *sync_name = "test_sync";
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  string sync_path = test_root + "/test2";
  TreeInfo tree_info;
  ret = zs::OsCreateDirectory(sync_path, true);
  CHECK_EQUAL(0, ret);
  zisync_ret = kernel->CreateTree(
      sync_info.sync_id, sync_path.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->CreateBackup(
      backup_name, sync_path.c_str(), &backup_info);
  CHECK_EQUAL(ZISYNC_ERROR_TREE_EXIST, zisync_ret);
}

TEST_FIXTURE(SomeFixture, QueryBackup) {
  err_t zisync_ret;
  zs::QueryBackupInfoResult result;
  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, static_cast<int>(result.backups.size()));

  const char *backup_name = "test";
  std::string pwd = test_root;
  string path1 = pwd + "/test1";
  int ret = zs::OsCreateDirectory(path1, true);
  CHECK_EQUAL(0, ret);
  
  string path2 = pwd + "/test2";
  ret = zs::OsCreateDirectory(path2, true);
  CHECK_EQUAL(0, ret);
  
  BackupInfo backup_info1;
  zisync_ret = kernel->CreateBackup(
      backup_name, path1.c_str(), &backup_info1);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  BackupInfo backup_info2;
  zisync_ret = kernel->CreateBackup(
      backup_name, path2.c_str(), &backup_info2);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  CHECK_EQUAL(2, static_cast<int>(result.backups.size()));

  CheckBackupInfoEqual(backup_info1, result.backups[0]);
  CheckBackupInfoEqual(backup_info2, result.backups[1]);

  BackupInfo backup_info;
  zisync_ret = kernel->QueryBackupInfo(
      backup_info1.backup_id, &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CheckBackupInfoEqual(backup_info1, backup_info);
  zisync_ret = kernel->QueryBackupInfo(
      backup_info1.backup_id + 100, &backup_info);
  CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  zisync_ret = kernel->QueryBackupInfo(
      backup_info2.backup_id, &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CheckBackupInfoEqual(backup_info2, backup_info);
}

// TEST_FIXTURE(SomeFixture, DestroyBackup) {
//   err_t zisync_ret;
//   const char *backup_name = "test";
//   std::string pwd = test_root;
//   string path1 = pwd + "/test1";
//   int ret = zs::OsCreateDirectory(path1, true);
//   CHECK_EQUAL(0, ret);
  
//   string path2 = pwd + "/test2";
//   ret = zs::OsCreateDirectory(path2, true);
//   CHECK_EQUAL(0, ret);
  
//   BackupInfo backup_info1;
//   zisync_ret = kernel->CreateBackup(
//       backup_name, path1.c_str(), &backup_info1);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   BackupInfo backup_info2;
//   zisync_ret = kernel->CreateBackup(
//       backup_name, path2.c_str(), &backup_info2);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   zisync_ret = kernel->DestroyBackup(backup_info1.backup_id);
//   zs::QueryBackupInfoResult result;
//   zisync_ret = QueryBackupInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   CHECK_EQUAL(1, static_cast<int>(result.backups.size()));
//   CheckBackupInfoEqual(backup_info2, result.backups[0]);

//   zisync_ret = kernel->DestroyBackup(backup_info1.backup_id);
//   CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
//   zisync_ret = kernel->DestroyBackup(backup_info2.backup_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

//   zisync_ret = QueryBackupInfo(&result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   CHECK_EQUAL(0, static_cast<int>(result.backups.size()));
// }

TEST_FIXTURE(SomeFixture, BackupToLocal) {
  err_t zisync_ret;
  const char *backup_name = "test";
  BackupInfo backup_info;
  std::string pwd = test_root;
  string path = pwd + "/test1";
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  
  zisync_ret = kernel->CreateBackup(
      backup_name, path.c_str(), &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(backup_name, backup_info.backup_name);
  CHECK_EQUAL(path, backup_info.src_tree.tree_root);
  CHECK_EQUAL(true, backup_info.src_tree.is_local);
  CHECK_EQUAL(true, backup_info.src_tree.is_sync_enabled);
  CHECK_EQUAL(true, backup_info.src_tree.device.is_mine);
  CHECK_EQUAL(zs::LOCAL_DEVICE_ID, backup_info.src_tree.device.device_id);
  CHECK_EQUAL(0, static_cast<int>(backup_info.target_trees.size()));

  TreeInfo backup_dst_tree;
  zisync_ret = kernel->AddBackupTargetDevice(
      backup_info.backup_id, zs::LOCAL_DEVICE_ID, &backup_dst_tree);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(true, backup_dst_tree.is_local);
  CHECK_EQUAL(true, backup_dst_tree.is_sync_enabled);
  CHECK_EQUAL(true, backup_dst_tree.device.is_mine);
  CHECK_EQUAL(zs::LOCAL_DEVICE_ID, backup_dst_tree.device.device_id);


  zs::QueryBackupInfoResult result;
  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(result.backups.size()));

  backup_info.target_trees.push_back(backup_dst_tree);
  CheckBackupInfoEqual(backup_info, result.backups[0]);
  
  /* create with exist */
  BackupInfo exit_backup_info;
  zisync_ret = kernel->CreateBackup(
      backup_name, backup_dst_tree.tree_root.c_str(), &exit_backup_info);
  CHECK_EQUAL(zs::ZISYNC_ERROR_BACKUP_DST_EXIST, zisync_ret);
  CheckBackupInfoEqual(backup_info, exit_backup_info);
  
  TreeInfo backup_dst_tree1;
  string backup_path = pwd + "/test2";
  zisync_ret = kernel->AddBackupTargetDevice(
      backup_info.backup_id, zs::LOCAL_DEVICE_ID, &backup_dst_tree1,
      backup_path.c_str());
  CHECK_EQUAL(zisync_ret, ZISYNC_SUCCESS);
  CHECK_EQUAL(true, backup_dst_tree1.is_local);
  CHECK_EQUAL(backup_path, backup_dst_tree1.tree_root);
  CHECK_EQUAL(true, backup_dst_tree1.is_sync_enabled);
  CHECK_EQUAL(true, backup_dst_tree1.device.is_mine);
  CHECK_EQUAL(zs::LOCAL_DEVICE_ID, backup_dst_tree1.device.device_id);

  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(result.backups.size()));

  backup_info.target_trees.push_back(backup_dst_tree1);
  CheckBackupInfoEqual(backup_info, result.backups[0]);

  zisync_ret = kernel->DelBackupTarget(backup_info.src_tree.tree_id);
  CHECK_EQUAL(zs::ZISYNC_ERROR_TREE_NOENT, zisync_ret);
  
  zisync_ret = kernel->DelBackupTarget(backup_dst_tree.tree_id + 100);
  CHECK_EQUAL(zs::ZISYNC_ERROR_TREE_NOENT, zisync_ret);
  
  zisync_ret = kernel->DelBackupTarget(backup_dst_tree1.tree_id);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(result.backups.size()));
  
  backup_info.target_trees.pop_back();
  CheckBackupInfoEqual(backup_info, result.backups[0]);
  
  zisync_ret = kernel->DelBackupTarget(backup_dst_tree.tree_id);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = QueryBackupInfo(&result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(result.backups.size()));
  
  backup_info.target_trees.pop_back();
  CheckBackupInfoEqual(backup_info, result.backups[0]);
}


TEST_FIXTURE(SomeFixture, ListBackupTargetDevice) {
  err_t zisync_ret;
  const char *backup_name = "test";
  BackupInfo backup_info;
  std::string pwd = test_root;
  string path = pwd + "/test1";
  int ret = zs::OsCreateDirectory(path, true);
  CHECK_EQUAL(0, ret);
  
  zisync_ret = kernel->CreateBackup(
      backup_name, path.c_str(), &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  zs::ListBackupTargetDeviceResult result;
  zisync_ret = kernel->ListBackupTargetDevice(backup_info.backup_id, &result);
  CHECK_EQUAL(1, static_cast<int>(result.devices.size()));
  CHECK_EQUAL(zs::LOCAL_DEVICE_ID, result.devices[0].device_id);
  CHECK_EQUAL(Config::device_name(), result.devices[0].device_name);
  CHECK_EQUAL(backup_root + "/" + Config::device_name(), 
              result.devices[0].backup_root);
  CHECK_EQUAL(true, result.devices[0].is_mine);
  CHECK_EQUAL(false, result.devices[0].is_backup);
  CHECK_EQUAL(true, result.devices[0].is_online);
  
  TreeInfo backup_dst_tree;
  zisync_ret = kernel->AddBackupTargetDevice(
      backup_info.backup_id, zs::LOCAL_DEVICE_ID, &backup_dst_tree);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->ListBackupTargetDevice(backup_info.backup_id, &result);
  CHECK_EQUAL(1, static_cast<int>(result.devices.size()));
  CHECK_EQUAL(zs::LOCAL_DEVICE_ID, result.devices[0].device_id);
  CHECK_EQUAL(Config::device_name(), result.devices[0].device_name);
  CHECK_EQUAL(backup_root + "/" + Config::device_name(), 
              result.devices[0].backup_root);
  CHECK_EQUAL(true, result.devices[0].is_mine);
  CHECK_EQUAL(true, result.devices[0].is_backup);
  CHECK_EQUAL(true, result.devices[0].is_online);
}

TEST_FIXTURE(SomeFixture, QuerySyncAndBackup) {
  err_t zisync_ret;
  const char *backup_name = "test_backup";
  std::string pwd = test_root;
  string path1 = pwd + "/test1";
  TreeInfo tree_info;
  int ret = zs::OsCreateDirectory(path1, true);
  CHECK_EQUAL(0, ret);
  string path2 = pwd + "/test2";
  ret = zs::OsCreateDirectory(path2, true);
  CHECK_EQUAL(0, ret);
  
  BackupInfo backup_info;
  zisync_ret = kernel->CreateBackup(
      backup_name, path2.c_str(), &backup_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  const char *sync_name = "test_sync";
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  zisync_ret = kernel->CreateTree(sync_info.sync_id, path1.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  sync_info.trees.push_back(tree_info);

  zs::QuerySyncInfoResult sync_result;
  zs::QueryBackupInfoResult backup_result;

  zisync_ret = QuerySyncInfo(&sync_result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(sync_result.sync_infos.size()));
  CheckSyncInfoEqual(sync_info, sync_result.sync_infos[0]);

  zisync_ret = QueryBackupInfo(&backup_result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, static_cast<int>(backup_result.backups.size()));
  CheckBackupInfoEqual(backup_info, backup_result.backups[0]);
}

TEST_FIXTURE(SomeFixture, Favorite_Case1) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_FAVOURITE_EXIST, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_FAVOURITE_EXIST, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test123"));
  CHECK_EQUAL(zs::FAVORITE_UNCANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test/test"));
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_FAVOURITE_NOENT, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test123");
  CHECK_EQUAL(zs::ZISYNC_ERROR_FAVOURITE_NOENT, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, kernel->HasFavorite(tree_info.tree_id));
  CHECK_EQUAL(zs::FAVORITE_NOT, kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(zs::ZISYNC_ERROR_FAVOURITE_NOENT, zisync_ret);
}

TEST_FIXTURE(SomeFixture, Favorite_Case2) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/fuck");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/"));
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/fuck"));
  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/fuck");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(true, kernel->HasFavorite(tree_info.tree_id));
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/test"));
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/"));
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/fuck"));
}

TEST_FIXTURE(SomeFixture, DelFavoriteRemoveFile) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";

  std::string path1 = root + "/test";
  std::string path2 = root + "/test/test";
  
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  ret = zs::OsCreateDirectory(path1, false);
  CHECK_EQUAL(0, ret);

  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  ret = zs::OsCreateDirectory(path2, false);
  CHECK_EQUAL(0, ret);

  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/test");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, zs::OsDirExists(path1.c_str()));
  CHECK_EQUAL(false, zs::OsDirExists(path2.c_str()));
  CHECK_EQUAL(true, zs::OsDirExists(root.c_str()));
}

TEST_FIXTURE(SomeFixture, DelFavoriteRoot) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";

  std::string path1 = root + "/test";
  
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);

  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  ret = zs::OsCreateDirectory(path1, false);
  CHECK_EQUAL(0, ret);

  zisync_ret = kernel->DelFavorite(tree_info.tree_id, "/");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, zs::OsDirExists(path1.c_str()));
  CHECK_EQUAL(true, zs::OsDirExists(root.c_str()));
}

TEST_FIXTURE(SomeFixture, DestorySyncDelFavorite) {
  err_t zisync_ret;
  int ret;
  std::string root = test_root;
  root += "/test";
  ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  int sync_id = sync_info.sync_id;
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->AddFavorite(tree_info.tree_id, "/");
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::FAVORITE_CANCELABLE, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/"));
  
  zisync_ret = kernel->DestroySync(sync_info.sync_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  zs::NormalSync sync;
  sync.set_uuid(sync_info.sync_uuid);
  zisync_ret = sync.Create(sync_info.sync_name);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(sync_id, sync_info.sync_id);
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(zs::FAVORITE_NOT, 
              kernel->GetFavoriteStatus(tree_info.tree_id, "/"));
}

TEST_FIXTURE(SomeFixture, InitializeDelDBFile) {
  std::string root = test_root;
  root += "/test";
  int ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  kernel->Shutdown();
  zisync_ret = kernel->Initialize(
      app_path.c_str(), "test", "test", backup_root.c_str());
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
}

TEST_FIXTURE(SomeFixture, ShutdownDelDBFile) {
  std::string root = test_root;
  root += "/test";
  int ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->DestroyTree(tree_info.tree_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  kernel->Shutdown();
  CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
}

TEST_FIXTURE(SomeFixture, SyncDelShutdownDelDBFile) {
  std::string root = test_root;
  root += "/test";
  int ret = zs::OsCreateDirectory(root, false);
  CHECK_EQUAL(0, ret);
  
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync("test", &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  TreeInfo tree_info;
  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->DestroySync(sync_info.sync_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  kernel->Shutdown();
  CHECK_EQUAL(false, IsTreeDBFileExisting(tree_info.tree_uuid.c_str()));
}

class EventListener : public zs::IEventListener {
 public:
  EventListener():has_modified(false) {}
  virtual void NotifySyncModify() { has_modified = true; }
  void reset() { has_modified = false; }
  bool has_modified;
};

TEST_FIXTURE(SomeFixture, DiscoverDevice_StartupAndShutdow) {
  const char *sync_name = "test";
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  int32_t discover_id;
  
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id + 1000, &discover_id);
  CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, discover_id);
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(1, discover_id);
  zisync_ret = kernel->ShutdownDiscoverDevice(0);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, discover_id);
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(2, discover_id);

  zisync_ret = kernel->ShutdownDiscoverDevice(3);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DISCOVER_NOENT, zisync_ret);

  for (int i = 3; i < 64; i ++) {
    zisync_ret = kernel->StartupDiscoverDevice(
        sync_info.sync_id, &discover_id);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
    CHECK_EQUAL(i, discover_id);
  }
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DISCOVER_LIMIT, zisync_ret);

  kernel->Shutdown();
  zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(0, discover_id);
}

TEST_FIXTURE(SomeFixture, GetDiscoveredDevice) {
  const char *sync_name = "test";
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  int32_t discover_id;
  zisync_ret = kernel->StartupDiscoverDevice(
      sync_info.sync_id, &discover_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zs::DiscoverDeviceResult result;
  zisync_ret = kernel->GetDiscoveredDevice(discover_id, &result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->GetDiscoveredDevice(discover_id + 1, &result);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DISCOVER_NOENT, zisync_ret);
  
  zisync_ret = kernel->ShutdownDiscoverDevice(discover_id);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->GetDiscoveredDevice(discover_id, &result);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DISCOVER_NOENT, zisync_ret);
}

// TEST_FIXTURE(SomeFixture, ShareSync) {
//   const char *sync_name = "test";
//   SyncInfo sync_info;
//   err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
//   int32_t discover_id;
//   zisync_ret = kernel->StartupDiscoverDevice(
//       sync_info.sync_id, &discover_id);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   zs::DiscoverDeviceResult result;
//   zisync_ret = kernel->GetDiscoveredDevice(discover_id, &result);
//   CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//   int32_t device_id = -1;
//   zisync_ret = kernel->ShareSync(
//       discover_id + 1, device_id, zs::SYNC_PERM_RDWR);
//   CHECK_EQUAL(zs::ZISYNC_ERROR_DISCOVER_NOENT, zisync_ret);
//   zisync_ret = kernel->ShareSync(
//       discover_id, device_id, zs::SYNC_PERM_RDWR);
//   CHECK_EQUAL(zs::ZISYNC_ERROR_DEVICE_NOENT, zisync_ret);
// }

TEST_FIXTURE(SomeFixture, Download) {
  const char *sync_name = "test";
  SyncInfo sync_info;
  err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  
  int32_t task_id;
  string target_path;
  zisync_ret = kernel->StartupDownload(
      sync_info.sync_id + 1, "/test", &target_path, &task_id);
  CHECK_EQUAL(ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  
  zisync_ret = kernel->StartupDownload(
      sync_info.sync_id, "/test", &target_path, &task_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(task_id, 0);

  zs::DownloadStatus result;
  zisync_ret = kernel->QueryDownloadStatus(task_id, &result);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->QueryDownloadStatus(task_id + 1, &result);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DOWNLOAD_NOENT, zisync_ret);

  zisync_ret = kernel->ShutdownDownload(task_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  zisync_ret = kernel->QueryDownloadStatus(task_id, &result);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DOWNLOAD_NOENT, zisync_ret);
  zisync_ret = kernel->ShutdownDownload(task_id);
  CHECK_EQUAL(zs::ZISYNC_ERROR_DOWNLOAD_NOENT, zisync_ret);
  
  zisync_ret = kernel->StartupDownload(
      sync_info.sync_id, "/test", &target_path, &task_id);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(task_id, 1);
}

//TEST_FIXTURE(SomeFixture, UpdateTreeRootPrefix) {
//  const char *sync_name = "test";
//  err_t zisync_ret;
//  SyncInfo sync_info;
//  TreeInfo tree_info;
//
//  zisync_ret = kernel->CreateSync(sync_name, &sync_info);
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//  std::string path = test_root;
//  path += "/test";
//  int ret = zs::OsCreateDirectory(path, true);
//  CHECK_EQUAL(0, ret);
//  
//  string root = test_root + "/test";
//  ret = zs::OsCreateDirectory(root, false);
//  CHECK_EQUAL(0, ret);
//
//  zisync_ret = kernel->CreateTree(sync_info.sync_id, root.c_str(), &tree_info);
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//
//  string tree_root_prefix = test_root;
//  tree_root_prefix[tree_root_prefix.length() - 1] ++;
//
//  kernel->Shutdown();
//  zisync_ret = kernel->Startup(
//      app_path.c_str(), 8848, NULL, tree_root_prefix.c_str());
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//
//  SyncInfo sync_info1;
//  zisync_ret = QuerySyncInfo(sync_info.sync_id, &sync_info1);
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//  CHECK_EQUAL(1, static_cast<int>(sync_info1.trees.size()));
//  CHECK_EQUAL(tree_root_prefix + "/test", sync_info1.trees[0].tree_root);
//  
//  tree_root_prefix = "/test";
//  kernel->Shutdown();
//  zisync_ret = kernel->Startup(
//      app_path.c_str(), 8848, NULL, tree_root_prefix.c_str());
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//  zisync_ret = QuerySyncInfo(sync_info.sync_id, &sync_info1);
//  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
//  CHECK_EQUAL(1, static_cast<int>(sync_info1.trees.size()));
//  CHECK_EQUAL(tree_root_prefix + "/test", sync_info1.trees[0].tree_root);
//}

// #ifndef _MSC_VER

TEST_FIXTURE(SomeFixture, DenyCreateTree) {
  Http::VerifyResponse response;
  response.set_errorcode(Http::Ok);
  Http::Permission *perm = response.add_permissions();
  assert(perm != NULL);
  perm->set_privilege(Http::CreateFolder);
  perm->set_constraint("1");

  int64_t time = zs::OsTimeInMs() + 1 * 3600;
  response.set_expiredtime(time);
  response.set_keycode(key);
  std::string content;
  bool b = response.SerializeToString(&content);
  assert(b == true);
  
  zs::GetPermission()->Reset(content, key);
  // create first tree
  {
    const char *sync_name = "test_deny1";
    SyncInfo sync_info;
    err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

    std::string tree_path = test_root + "/test_deny1";
    int ret = zs::OsCreateDirectory(tree_path, false);
    CHECK_EQUAL(0, ret);

    TreeInfo tree_info;
    zisync_ret = kernel->CreateTree(
        sync_info.sync_id, tree_path.c_str(), &tree_info);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  }

  // create second tree
  {
    const char *sync_name = "test_deny2";
    SyncInfo sync_info;
    err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }
  
}

TEST_FIXTURE(SomeFixture, DenyShareSync) {
  Http::VerifyResponse response;
  response.set_errorcode(Http::Ok);
  int64_t time = zs::OsTimeInMs() + 1 * 3600;
  std::string str_time;
  zs::StringFormat(&str_time, "%" PRId64, time);
  response.set_expiredtime(time);
  response.set_keycode(key);
  {
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::CreateFolder);
    perm->set_constraint("1");
  }

  {
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::ShareSwitch);
    perm->set_constraint("0");
  }

  std::string content;
  bool b = response.SerializeToString(&content);
  assert(b == true);
  
  zs::GetPermission()->Reset(content, key);
  // create tree
  {
    const char *sync_name = "test_deny1";
    SyncInfo sync_info;
    err_t zisync_ret = kernel->CreateSync(sync_name, &sync_info);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

    std::string tree_path = test_root + "/test_deny1";
    int ret = zs::OsCreateDirectory(tree_path, false);
    CHECK_EQUAL(0, ret);

    TreeInfo tree_info;
    zisync_ret = kernel->CreateTree(
        sync_info.sync_id, tree_path.c_str(), &tree_info);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

    zisync_ret = kernel->ShareSync(1, 1, zs::SYNC_PERM_RDONLY);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }
}


TEST(test_VerifyCDKey) {
  
  std::string key = "D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU";
  CHECK(kernel->VerifyCDKey(key) == ZISYNC_SUCCESS);

  key = "DNDLF-NCOH2-BJICC-1PD6T-JA1QI-CKRUU";
  CHECK(kernel->VerifyCDKey(key) == ZISYNC_SUCCESS);

  key = "11111-D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU";
  CHECK(kernel->VerifyCDKey(key) != ZISYNC_SUCCESS);

  key = "D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU-1111";
  CHECK(kernel->VerifyCDKey(key) != ZISYNC_SUCCESS);

  key = "";
  CHECK(kernel->VerifyCDKey(key) != ZISYNC_SUCCESS);

  key = "12ejwelfj;wsf9w8fs;odjf;sojif";
  CHECK(kernel->VerifyCDKey(key) != ZISYNC_SUCCESS);

  key = "D18S3-VJL27-CONQ9-8TN23-JY7W9-KZDEZ";
  CHECK(kernel->VerifyCDKey(key) != ZISYNC_SUCCESS);
}

// #ifndef _MSC_VER
//TEST_FIXTURE(SomeFixture, DenyOnlineScanf) {
//  {
//    Http::VerifyResponse response;
//    response.set_errorcode(Http::Ok);
//    Http::Permission *perm = response.add_permissions();
//    assert(perm != NULL);
//    perm->set_privilege(Http::OnlineOpen);
//    perm->set_constraint("1");
//    int64_t time = zs::OsTimeInMs() + 1 * 3600;
//    response.set_expiredtime(time);
//    response.set_keycode(key);
// 
//    std::string content;
//    bool b = response.SerializeToString(&content);
//    assert(b == true);
//    zs::GetPermission()->Reset(content);
//    zs::ListSyncResult list_sync_result;
//    err_t zisync_ret = kernel->ListSync(1, std::string(), &list_sync_result);
//    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
//  }
//
//  {
//    Http::VerifyResponse response;
//    response.set_errorcode(Http::Ok);
//    Http::Permission *perm = response.add_permissions();
//    assert(perm != NULL);
//    perm->set_privilege(Http::OnlineSwitch);
//    perm->set_constraint("1");
//    int64_t time = zs::OsTimeInMs() + 1 * 3600;
//    response.set_expiredtime(time);
//    response.set_keycode(key);
//
//    std::string content;
//    bool b = response.SerializeToString(&content);
//    assert(b == true);
//    zs::GetPermission()->Reset(content);
//    zs::ListSyncResult list_sync_result;
//    err_t zisync_ret = kernel->ListSync(1, std::string(), &list_sync_result);
//    CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
//  }
//}

TEST_FIXTURE(SomeFixture, DenyOnlineScanfDownload) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->StartupDownload(1, std::string(), NULL, NULL);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }

  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineDownload);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->StartupDownload(1, std::string(), NULL, NULL);
    CHECK_EQUAL(zs::ZISYNC_ERROR_INVALID_PATH, zisync_ret);
  }
}

TEST_FIXTURE(SomeFixture, DenyOnlineScanfUpload) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->StartupUpload(
        1, std::string(), std::string(), NULL);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }

  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineUpload);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->StartupUpload(
        1, std::string(), std::string(), NULL);
    CHECK_EQUAL(zs::ZISYNC_ERROR_INVALID_PATH, zisync_ret);
  }
}

TEST_FIXTURE(SomeFixture, DenyTransferList) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    
    zs::GetPermission()->Reset(content, key);
    zs::TransferListStatus transfer_list;
    err_t zisync_ret = kernel->QueryTransferList(1, &transfer_list, 0, 100);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }

  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::TransferSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    zs::TransferListStatus transfer_list;
    err_t zisync_ret = kernel->QueryTransferList(1, &transfer_list, 0, 100);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  }
}

TEST_FIXTURE(SomeFixture, DenyHistory) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    
    zs::GetPermission()->Reset(content, key);
    zs::QueryHistoryResult histroies;
    err_t zisync_ret = kernel->QueryHistoryInfo(&histroies, 100);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }

  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::HistorySwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    zs::QueryHistoryResult histroies;
    err_t zisync_ret = kernel->QueryHistoryInfo(&histroies, 100);
    CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  }
}

TEST_FIXTURE(SomeFixture, DenyChangeSharePermission) {
  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    Http::Permission *perm = response.add_permissions();
    assert(perm != NULL);
    perm->set_privilege(Http::OnlineSwitch);
    perm->set_constraint("1");
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->SetShareSyncPerm(1, 1, zs::SYNC_PERM_RDONLY);
    CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
  }

  {
    Http::VerifyResponse response;
    response.set_errorcode(Http::Ok);
    int64_t time = zs::OsTimeInMs() + 1 * 3600;
    response.set_expiredtime(time);
    response.set_keycode(key);
    {
      Http::Permission *perm = response.add_permissions();
      assert(perm != NULL);
      perm->set_privilege(Http::ChangeSharePermission);
      perm->set_constraint("1");
    }
    {
      Http::Permission *perm = response.add_permissions();
      assert(perm != NULL);
      perm->set_privilege(Http::ShareRead);
      perm->set_constraint("1");
    }

    std::string content;
    bool b = response.SerializeToString(&content);
    assert(b == true);
    zs::GetPermission()->Reset(content, key);
    err_t zisync_ret = kernel->SetShareSyncPerm(1, 1, zs::SYNC_PERM_RDONLY);
    CHECK_EQUAL(zs::ZISYNC_ERROR_SYNC_NOENT, zisync_ret);
  }
}

//TEST_FIXTURE(SomeFixture, TestSaveMacAddress) {
//  std::string mac_address("test");
//  err_t zisync_ret = zs::GetLicences()->SaveMacAddress(mac_address);
//  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
//  CHECK_EQUAL(mac_address, zs::GetLicences()->mac_address());
//}

TEST_FIXTURE(SomeFixture, TestSavePerKey) {
  std::string key("test");
  err_t zisync_ret = zs::GetLicences()->SavePermKey(key);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(key, zs::GetLicences()->perm_key());
}

TEST_FIXTURE(SomeFixture, TestSaveExpiredTime) {
  int64_t expired_time = 10000000;
  err_t zisync_ret = zs::GetLicences()->SaveExpiredTime(expired_time);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expired_time, zs::GetLicences()->expired_time());
}

TEST_FIXTURE(SomeFixture, TestSaveCreatedTime) {
  int64_t created_time = 1000000;
  err_t zisync_ret = zs::GetLicences()->SaveCreatedTime(created_time);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(created_time, zs::GetLicences()->created_time());
}

TEST_FIXTURE(SomeFixture, TestSaveLastContactTime) {
  int64_t last_contact_time = 10000000000;
  err_t zisync_ret = zs::GetLicences()->SaveLastContactTime(last_contact_time);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(last_contact_time, zs::GetLicences()->last_contact_time());
}

TEST_FIXTURE(SomeFixture, TestSaveExpiredOfflineTime) {
  int64_t expired_offline_time = 100000000000;
  err_t zisync_ret = zs::GetLicences()->SaveExpiredOfflineTime(expired_offline_time);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(expired_offline_time, zs::GetLicences()->expired_offline_time());
}

TEST_FIXTURE(SomeFixture, TestSaveRole) {
  std::string role = "Trail";
  err_t zisync_ret = zs::GetLicences()->SaveRole(role);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);
  CHECK_EQUAL(role, zs::GetLicences()->role());
}

TEST_FIXTURE(SomeFixture, TestStaticPeersDeny) {
  zs::ListStaticPeers peers;
  zs::IpPort port("192.168.1.12", 8848);
  peers.peers.push_back(port);
  err_t zisync_ret = kernel->AddStaticPeers(peers);
  CHECK_EQUAL(ZISYNC_SUCCESS, zisync_ret);

  Http::VerifyResponse response;
  response.set_errorcode(Http::Ok);
  Http::Permission *perm = response.add_permissions();
  assert(perm != NULL);
  perm->set_privilege(Http::DeviceEdit);
  perm->set_constraint("0");
  response.set_expiredtime(zs::OsTimeInS() + 1 * 3600);
  response.set_keycode(key);

  std::string content;
  bool b = response.SerializeToString(&content);
  assert(b == true);
  zs::GetPermission()->Reset(content, key);
  zisync_ret = kernel->AddStaticPeers(peers);
  CHECK_EQUAL(zs::ZISYNC_ERROR_PERMISSION_DENY, zisync_ret);
}

TEST_FIXTURE(SomeFixture, TestFeedbackInterface) {
  err_t ret = ZISYNC_SUCCESS;
  std::string type("This is feedback type.");
  std::string version("This is product version.");
  std::string message("This is content.");
  std::string contack("This is contact info.");

  ret = kernel->Feedback(type, version, message, contack);
  assert(ret == ZISYNC_SUCCESS);

  type.assign("备份type");
  ret = kernel->Feedback(type, version, message, contack);
  assert(ret == ZISYNC_SUCCESS);
  ZSLOG_INFO("%s", "Wait 10 sec for server to response.");
  sleep(10);
  ZSLOG_INFO("%s", "Wait done.");

}

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

int main(int argc, char** argv) {
  logger.Initialize();
  // logger.error_to_stderr = true;
  // logger.info_to_stdout = true;
  int ret = OsGetFullPath(".", &app_path);
  assert(ret == 0);
  backup_root = app_path + "/Backup";
  test_root = app_path + "/Test";
    
  LogInitialize(&logger);
  kernel = zs::GetZiSyncKernel("actual");
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
// #endif 
