// // Copyright 2014, zisync.com
// 
#include <memory>
#include <cassert>
#include <iostream>
#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <UnitTest++/TestReporterStdout.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/history/history.h"
#include "zisync/kernel/history/history_data_source.h"
#include "zisync/kernel/history/history_manager.h"
#include "zisync/kernel/permission.h"
#include "zisync/kernel/proto/verify.pb.h"
#include "zisync/kernel/database/content.h"
using zs::err_t;

using zs::IZiSyncKernel;
using zs::ZISYNC_SUCCESS;
using zs::ZISYNC_ERROR_ADDRINUSE;
using zs::ZISYNC_ERROR_CONFIG;
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
using zs::OsUdpSocket;
using zs::DefaultLogger;
using zs::FindRequest;
using zs::MsgStat;
using zs::ZSLOG_ERROR;
using std::string;
using zs::StringFormat;
using zs::OsGetFullPath;
using zs::OsThread;
using zs::IHistoryManager;
using namespace zs;

static IHistoryManager *hm;
static IZiSyncKernel *kernel;
static DefaultLogger logger("./Log");
std::string app_path;
std::string backup_root;
std::string test_root;

static int64_t last_time = -1;

static inline void PrintTime(const char *prefix) {
  if (last_time != -1 && prefix != NULL) {
    ZSLOG_ERROR("%s : %" PRId64, prefix, zs::OsTimeInMs() - last_time);
  }
  last_time = zs::OsTimeInMs();
}

void ClearKernelResources() {
  PrintTime("OnTest");
  kernel->Shutdown();
  PrintTime("Shutdown");
  int ret = zs::OsDeleteDirectories(test_root);
  assert(ret == 0);
  ret = zs::OsDeleteDirectories(backup_root);
  assert(ret == 0);
}

//void SetupPermission() {
//// for user permission
//    std::string mac_address = "test";
//    std::string key = "test";
//    zs::SavePermKey(key);
//    zs::SaveMacAddress(mac_address);
//    std::map<Http::PrivilegeCode, std::string> perms = {
//      {Http::CreateFolder, "-1"},
//      {Http::ShareSwitch, "-1"},
//      {Http::ShareReadWrite, "-1"},
//      {Http::ShareRead, "-1"},
//      {Http::ShareWrite, "-1"},
//      {Http::DeviceSwitch, "-1"},
//      {Http::DeviceEdit, "-1"},
//      {Http::OnlineSwitch, "-1"},
//      {Http::OnlineOpen, "-1"},
//      {Http::OnlineDownload, "-1"},
//      {Http::OnlineUpload, "-1"},
//      {Http::TransferSwitch, "-1"},
//      {Http::HistorySwitch, "-1"},
//      {Http::ChangeSharePermission, "-1"},
//      {Http::RemoveShareDevice, "-1"},
//    };
//
//    Http::VerifyResponse response;
//    response.set_errorcode(Http::Ok);
//    int64_t time = zs::OsTimeInMs() + 1 * 3600;
//    response.set_expiredtime(time);
//    response.set_keycode(key);
//    response.set_role(Http::Premium);
//    response.set_expiredofflinetime(-1);
//    response.set_lastcontacttime(-1);
//    response.set_createdtime(-1);
//    for (auto it = perms.begin(); it != perms.end(); it++) {
//     Http::Permission *perm = response.add_permissions();
//     assert(perm != NULL);
//     perm->set_privilege(it->first);
//     perm->set_constraint(it->second);
//    }
//    zs::GetPermission()->Reset(response);
//
//
//}
//
struct SomeFixture {
  SomeFixture() {
    err_t zisync_ret;

    PrintTime(NULL);
    zs::OsCreateDirectory(test_root, false);
    zs::OsCreateDirectory(backup_root, false);
    zisync_ret = kernel->Initialize(
        app_path.c_str(), "zisyncsw", "zisyncsw", backup_root.c_str());
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
    assert(zisync_ret == ZISYNC_SUCCESS);
    //SetupPermission();
    
    PrintTime("Prepare");
  }
  ~SomeFixture() {
    
  }
};


static inline bool IsTreeDBFileExisting(const char *tree_uuid) {
  std::string tree_db_path = Config::database_dir();
  zs::StringFormat(&tree_db_path,
                   "%s/%s.db", Config::database_dir().c_str(),
                   tree_uuid);
  return zs::OsFileExists(tree_db_path);
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

static string cmd_;

class QueryAndSync : public OsThread{
    public:
    QueryAndSync():OsThread("QueryAndSync"){}
    virtual int Run() {
        err_t error = kernel->Startup(app_path.c_str(), 8848, NULL);
        if (error == ZISYNC_ERROR_CONFIG) {
            error = kernel->Initialize(
                app_path.c_str(), "zisyncsw", "zisyncsw", backup_root.c_str());
            assert(error == ZISYNC_SUCCESS);
            error = kernel->Startup(app_path.c_str(), 8848, NULL);
            assert(error == ZISYNC_SUCCESS);
            //SetupPermission();
            if(cmd_ == "giver") {
              SyncInfo sync_info;
              error = kernel->CreateSync("iSync", &sync_info);
              assert(error == ZISYNC_SUCCESS);
              std::string path = test_root;
              path += "/test-" + kernel->GetDeviceName();
              zs::OsCreateDirectory(path, false);
              error = kernel->CreateTree(
                  sync_info.sync_id, (path + "/").c_str(), NULL);
              assert(error == ZISYNC_SUCCESS);
            }

        }else if(error != ZISYNC_SUCCESS) {
            assert(false);
            return -1;
        }
      SyncInfo sync_info;
      TreeInfo tree_info;
      hm = GetHistoryManager();
        while(true) {
            
          if (toggle) {
            std::cout << "Syncing.." << std::endl;
            zs::QuerySyncInfoResult result;
            error = QuerySyncInfo(&result);
            assert(error == ZISYNC_SUCCESS);
            for( int i = 0; i < (int)result.sync_infos.size(); i++ ) {
              SyncInfo &sync = result.sync_infos[(unsigned)i];
              if (cmd_ == "rdonly") {
                kernel->SetSyncPerm(sync.sync_id, TableSync::PERM_RDONLY);
              }
              std::cout << "Sync: " <<  sync.sync_name << std::endl;
              bool has_local_tree = false;
              for(int j = 0; j < (int)sync.trees.size(); j++) {
                TreeInfo &tree = sync.trees[(unsigned)j];
                std::cout << "Tree: " << tree.tree_root << std::endl;
                kernel->AddFavorite(tree.tree_id, "/");
                if (tree.is_local) {
                  has_local_tree = true;
                }
              }
              if (!has_local_tree) {
                string rt_ = test_root;
                rt_ += "/root-";
                rt_ += sync.sync_name;
                zs::OsCreateDirectory(rt_, false);
                error = kernel->CreateTree(sync.sync_id, rt_.c_str(), NULL);
                assert(error == ZISYNC_SUCCESS);
              }
            }

          } else {            
            std::cout << "Querying.." << std::endl;
            QueryHistoryResult queryRes;
            kernel->QueryHistoryInfo(&queryRes);
            vector<History> &query = queryRes.histories;
            for (auto it = query.begin(); it != query.end(); ++it) {
              std::cout << it->modifier << " " << it->backup_type  << " " << it->frompath << " " << it->time_stamp << " " << it->code << " " << it->error << std::endl;
            }
          }
          sleep(1);
          toggle = !toggle;
        }

        return 0;
    }
    private:
      bool toggle;
};
QueryAndSync qs;

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

    zs::OsCreateDirectory(test_root, false);
    zs::OsCreateDirectory(backup_root, false);

    LogInitialize(&logger);
    kernel = zs::GetZiSyncKernel("actual");
    UnitTest::RunAllTests();
    if(argc == 2) {

        cmd_ = argv[1];
    }
    qs.Startup();
    qs.Shutdown();
    ClearKernelResources();
    logger.CleanUp();
    return 0;
}
// #endif 
