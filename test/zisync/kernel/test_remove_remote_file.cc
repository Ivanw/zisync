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
#include "zisync/kernel/history/history_data_source.h"
#include "zisync/kernel/history/history_manager.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/zmq.h"
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

//err_t SendRemoveRemoteRequestToOuterWorker(const std::string &sync_uuid
//    , const std::string &path) {
//  err_t zisync_ret;
//  const ZmqContext &context = GetGlobalContext();
//  ZmqSocket *sock = new ZmqSocket(context, ZMQ_REQ);
//  RemoveRemoteFileRequest request;
//  RemoveRemoteFileResponse response;
//
//  MsgRemoveRemoteFileRequest *mutable_request = request.mutable_request();
//  mutable_request->set_sync_uuid(sync_uuid);
//  mutable_request->set_relative_path(path);
//
//  string remote_route_uri;
//  StringFormat(&remote_route_uri, "tcp://%s:%d", "*", 
//              Config::route_port() );
//  zisync_ret = request.SendTo(*sock);
//  assert(zisync_ret == ZISYNC_SUCCESS);
//
//  std::string uuid;
//  response.RecvFrom(*sock, 0, &uuid);
//  assert(zisync_ret == ZISYNC_SUCCESS);
//  delete sock;
//  return  ZISYNC_SUCCESS;
//}
//
void CountDown(int secs, const std::string &before,
    const std::string &after) {
  std::cout << before.c_str() << std::endl;
  for(; secs > 0; --secs) {
    std::cout << secs << " .." << std::endl;
    sleep(1);
  }
  std::cout << after.c_str() << std::endl;
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
            if(cmd_ == "giver") {
              SyncInfo sync_info;
              kernel->CreateSync("iSync", &sync_info);
              std::string path = test_root;
              path += "/test-" + kernel->GetDeviceName();
              zs::OsCreateDirectory(path, false);
              kernel->CreateTree(
                  sync_info.sync_id, (path + "/").c_str(), NULL);
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
            ListSyncResult list_sync;
            zs::QuerySyncInfoResult result;
            error = QuerySyncInfo(&result);
            assert(error == ZISYNC_SUCCESS);
            for( int i = 0; i < (int)result.sync_infos.size(); i++ ) {
              SyncInfo &sync = result.sync_infos[(unsigned)i];

              bool delete_done = false;
              list_sync.files.clear();
              kernel->ListSync(sync.sync_id, "/", &list_sync);
              const vector<FileMeta> &files = list_sync.files;
              for(int j = 0; j < (int)files.size(); j++) {
                CountDown(2, "To Send Remove Request after CountDown:", "Sending ..");
                std::string fixed_path = "/";
                if (files[j].name.at(0) != '/') {
                  fixed_path.append(files[j].name);
                }else{
                  fixed_path.append(files[j].name.c_str() + 1);
                }
                std::cout << "Trying to delete file: " << files[j].name << std::endl;
                if (kernel->RemoveRemoteFile(sync.sync_id
                      , fixed_path)
                    == ZISYNC_SUCCESS) {

                  delete_done = true;
                  std::cout << "File (" 
                    << files[j].name 
                    << ") removed." 
                    << std::endl;

                  CountDown(2, "Wait for check ..", "Continue.");
                  break;
                }else {
                  assert(false);
                }
              }
              if (delete_done) {
                break;
              }
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
