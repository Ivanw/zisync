#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"  // NOLINT
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <iostream>
#include <UnitTest++/UnitTest++.h>
// #include <UnitTest++/TestReporterStdout.h>
#include "zisync/kernel/test_reporter_dox_stdout.h"

#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/tree_status.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/test_tool_func.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/proto/kernel.pb.h"

using zs::err_t;
using zs::ITransferServer;
using zs::IPutTask;
using zs::IGetTask;
using zs::IUploadTask;
using zs::ITreeAgent;
using zs::IPutHandler;
using zs::ISslAgent;
using zs::StringFormat;
using zs::OsThread;
using zs::DefaultLogger;
using zs::TreeStat;
using zs::OsFileStat;
using zs::TreeStatus;
using zs::ST_PUT;
using zs::ST_GET;
using zs::ILibEventBase;
using zs::ITaskMonitor;
using std::string;
using zs::TaskMonitor;
using zs::StatusType;
using zs::MsgPushSyncMeta;
using zs::MsgRemoteMeta;
using zs::MsgStat;
using zs::OsPathAppend;
using zs::OsFile;

int32_t GetTreeId(const std::string& tree_uuid) {
  if (tree_uuid == "src_put") {
    return 0;
  } else if (tree_uuid == "dst_put") {
    return 1;
  } else if (tree_uuid == "local_get") {
    return 2;
  } else if (tree_uuid == "remote_get") {
    return 3;
  }

  return 1;
}

class TreeAgent : public ITreeAgent {
 public:
  TreeAgent() : is_using_task_monitor(true) {
  }
  virtual ~TreeAgent() {}

  virtual std::string GetTreeRoot(const std::string& tree_uuid) {
    std::string tree_root;
    if (tree_uuid == "src_put") {
      tree_root = "transfer/src";
    } else if (tree_uuid == "dst_put") {
      tree_root = "transfer/dest";
    } else if (tree_uuid == "local_get") {
      tree_root = "transfer/dest/get_file";
    } else if (tree_uuid == "remote_get") {
      tree_root = "transfer/src";
    } else if (tree_uuid == "error") {
      tree_root = "transfer/dest/error";
    }

    return tree_root;
  }

  virtual std::string GetNewTmpDir(const std::string& tree_uuid) {
    std::string tmp_dir;
    if (tree_uuid == "dst_put") {
      tmp_dir = "transfer/dest/put_tmp_dir";
    } else if (tree_uuid == "local_get") {
      tmp_dir = "transfer/dest/get_file";
    } else if (tree_uuid == "error") {
      tmp_dir = "transfer/dest/error";
    }

    return tmp_dir;
  }

  virtual int32_t GetTreeId(const std::string& tree_uuid) {
    return ::GetTreeId(tree_uuid);
  }
  virtual int32_t GetSyncId(const std::string& tree_uuid) {
    return 0;
  }
  virtual std::string GetTreeUuid(const int32_t tree_id) {
    switch (tree_id) {
      case 0:
        return "src_put";
      case 1:
        return "dst_put";
      case 2:
        return "local_get";
      case 3:
        return "remote_get";
    }

    return std::string();
  }
  virtual bool AllowPut(
      int32_t local_tree_id, int32_t remote_tree_id,
      const std::string& relative_path) {
    return true;
  }
  virtual bool TryLock(int32_t local_tree_id, int32_t remote_tree_id) {
    return true;
  }
  virtual void Unlock(int32_t local_tree_id, int32_t remote_tree_id) {
  }

  virtual IPutHandler* CreatePutHandler(const std::string &tmp_root) {
    return NULL;
  }
  virtual IPutHandler* CreateUploadHandler(
      const std::string &tree_uuid, const std::string &tmp_root) {
    return NULL;
  }

  virtual ITaskMonitor* CreateTaskMonitor(
      zs::TaskType type,
      const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid,
      int32_t total_files, int64_t total_bytes) {
    if (is_using_task_monitor == false) {
      return NULL;
    }

    int32_t local_tree_id = GetTreeId(local_tree_uuid);
    int32_t remote_tree_id = GetTreeId(remote_tree_uuid);
    StatusType st = (type == zs::TASK_TYPE_PUT) ? ST_PUT : ST_GET;

    return new TaskMonitor(
        local_tree_id, remote_tree_id, st, total_files, total_bytes);
  }

  virtual string GetAlias(const string&, const string&, const string&) {
    return string();
  }

 public:
  bool is_using_task_monitor;

};

int handler_file(std::string tmp_root, std::string root) {
  DIR* dir = NULL;
  struct dirent* rc = NULL;
  dir = opendir(tmp_root.c_str());
  assert(dir != NULL);
  while ((rc = readdir(dir)) != NULL) {
    if (strcmp(rc->d_name, "..") == 0 ||
        strcmp(rc->d_name, ".") == 0) {
      continue;
    }
    std::string tmp_path = tmp_root + "/" + rc->d_name;
    std::string save_path = root + "/" + rc->d_name;
    if (rc->d_type == DT_DIR) {
      int ret = zs::OsCreateDirectory(save_path, false);
      assert(ret == 0);
      ret = handler_file(tmp_path, save_path);
      assert(ret == 0);
    } else if (rc->d_type == DT_REG){
      int ret = rename(tmp_path.c_str(), save_path.c_str());
      if (ret != 0) {
        perror("link");
      }
    }
  }
  closedir(dir);

  return 0;
}

class PutHandler : public IPutHandler {
 public:
  virtual ~PutHandler() {}

  virtual err_t OnHandlePut(const std::string& tmp_root) {
    std::string root;
    if (tmp_root == "transfer/dest/error") {
      return zs::ZISYNC_ERROR_PUT_FAIL;
    } else {
      if (tmp_root == "transfer/dest/get_tmp_dir") {
        root = "transfer/dest/get_file";
      } else if (tmp_root == "transfer/dest/put_tmp_dir" ||
                 tmp_root == "transfer/dest/put_file_3K" ||
                 tmp_root == "transfer/dest/put_file_3M" ||
                 tmp_root == "transfer/dest/put_file_3G" ||
                 tmp_root == "transfer/dest/put_file_sysmbol") {
        root = "transfer/dest/put_file";
      }
    }

    int ret = handler_file(tmp_root, root);
    assert(ret == 0);
    return zs::ZISYNC_SUCCESS;
  }

  virtual bool OnHandleFile(
      const std::string& relative_path, 
      const std::string& real_path, const std::string& sha1) {
    return true;
  }  
};

class SslAgent : public ISslAgent {
 public:
  ~SslAgent() {}

  virtual std::string GetCertificate() {
    std::string certificate_path("transfer/server.crt");
    return certificate_path;
  }
  virtual std::string GetPrivateKey() {
    std::string private_key_path("transfer/server.key");
    return private_key_path;
  }
  virtual std::string GetCaCertificate() {
    std::string ca_certificate_path("transfer/ca.crt");
    return ca_certificate_path;
  }
};


struct TransferFixture {
  TransferFixture() {
    server_ = zs::GetTransferServer2();

    uri = "tcp://127.0.0.1:9525";
    local_put_tree_uuid = "src_put";
    remote_put_tree_uuid = "dst_put";

    local_get_tree_uuid = "local_get";
    remote_get_tree_uuid = "remote_get";

    local_get_dir = "transfer/dest/get_file/";
    remote_get_dir = "transfer/src/";
    put_src_dir = "transfer/src/";
    put_dst_dir = "transfer/dest/put_tmp_dir/";

    // startup libevent for transfer list
    ILibEventBase *event_base_tl = zs::GetEventBaseDb();
    event_base_tl->Startup();

    ITransferServer *server = zs::GetTransferServer2();
    CHECK(server != NULL);

    tree_agent_ = new TreeAgent();
    put_handler_ = new PutHandler;

    server->Initialize(9525, tree_agent_, NULL);

    ILibEventBase* event_base = zs::GetEventBase();
    event_base->RegisterVirtualServer(server);
    event_base->Startup(); 
  }

  ~TransferFixture() {

    // shutdown libevent of transfer list
    ILibEventBase *event_base_tl = zs::GetEventBaseDb();
    event_base_tl->Shutdown();

    ILibEventBase* event_base = zs::GetEventBase();
    event_base->Shutdown(); 

    ITransferServer *server = zs::GetTransferServer2();
    CHECK(server != NULL);

    event_base->UnregisterVirtualServer(server);
    server->CleanUp();

    if (tree_agent_) {
      delete tree_agent_;
      tree_agent_ = NULL;
    }
    if (put_handler_) {
      delete put_handler_;
      put_handler_ = NULL;
    }

  }

  ITreeAgent* tree_agent_;
  IPutHandler* put_handler_;
  TaskMonitor* put_monitor_;
  TaskMonitor* get_monitor_;
  ITransferServer* server_;
  std::string uri;

  std::string local_put_tree_uuid;
  std::string remote_put_tree_uuid;

  std::string local_get_tree_uuid;
  std::string remote_get_tree_uuid;

  std::string local_get_dir;
  std::string remote_get_dir;
  std::string put_src_dir;
  std::string put_dst_dir;

};

static int32_t CreateMetaFile(const std::string &local_tree_uuid,
                              const std::string &remote_tree_uuid,
                              const std::string &tree_root,
                              const std::vector<std::string> &files) {
  MsgPushSyncMeta push_sync_meta;
  push_sync_meta.set_local_tree_uuid(local_tree_uuid);
  push_sync_meta.set_remote_tree_uuid(remote_tree_uuid);

  MsgRemoteMeta *remote_meta = push_sync_meta.mutable_remote_meta();

  for (auto it = files.begin(); it != files.end(); it++) {
    std::string file_path = tree_root;
    OsFileStat st;
    OsPathAppend(&file_path, *it);

    CHECK_EQUAL(0, OsStat(file_path, std::string(), &st));

    MsgStat* msg_stat = remote_meta->add_stats();
    assert(msg_stat != NULL);
    msg_stat->set_path(*it);
    msg_stat->set_type(zs::FT_REG);
    msg_stat->set_status(zs::FS_NORMAL);
    msg_stat->set_mtime(st.mtime);
    msg_stat->set_length(st.length);
    msg_stat->set_usn(0);
    msg_stat->set_sha1(std::string());
    msg_stat->set_win_attr(0);
    msg_stat->set_unix_attr(0);
    msg_stat->set_android_attr(0);
    msg_stat->add_vclock(0);
  }

  std::string data = push_sync_meta.SerializeAsString();
  std::string meta_file_path = tree_root;
  OsPathAppend(&meta_file_path, "/.zisync.meta");

  OsFile file;
  CHECK_EQUAL(0, file.Open(meta_file_path.c_str(), string(), "wb"));

  CHECK_EQUAL(data.size(), file.Write(data));

  return data.size();
}

TEST_FIXTURE(TransferFixture, test_TransferServerStartAndStop) {
  // nothing to do
}

TEST_FIXTURE(TransferFixture, test_TransferServerPutEmptyFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_empty");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);

  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerPutSmallFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_3M");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerPutLargeFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_3G");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerPutSymbolFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_symbol");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}


TEST_FIXTURE(TransferFixture, test_TransferServerPutDeepDirFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("dir1/dir2/dir3/file_3K");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);
  IPutTask* put_task =zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerMultipleFile) {
  CHECK(system("transfer/test_server.sh") != -1);
  err_t eno;
  OsFileStat file_stat;
  int64_t files_size = 0;
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files = {
    "file_3K",
    "file_3M",
    "file_3G",
    "dir1/dir2/dir3/file_3K",
  };

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);


  for (size_t i = 0; i < files.size(); i++) {
    CHECK_EQUAL(0, OsStat(put_src_dir + files[0], string(), &file_stat));
    files_size += file_stat.length;
  }

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      files.size(), files_size);

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  for (size_t i = 0; i < files.size(); i++) {
    CHECK_EQUAL(0, OsStat(put_src_dir + files[0], string(), &file_stat));
    monitor->AppendFile(put_src_dir + files[i],
                        put_dst_dir + files[i],
                        files[i],
                        file_stat.length);

    eno = put_task->AppendFile(
        put_src_dir + files[i], files[i], string(), file_stat.length);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  }

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + files[0], put_dst_dir + files[0]));
  CHECK(zs::FileIsEqual(put_src_dir + files[1], put_dst_dir + files[1]));
  CHECK(zs::FileIsEqual(put_src_dir + files[2], put_dst_dir + files[2]));
  CHECK(zs::FileIsEqual(put_src_dir + files[3], put_dst_dir + files[3]));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetEmptyFile) {
  std::string filename("file_empty");
  OsFileStat file_stat;

  CHECK_EQUAL(0, OsStat(remote_get_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      1, file_stat.length);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  monitor->AppendFile(local_get_dir + filename, remote_get_dir + filename,
                      filename, file_stat.length);

  err_t eno = get_task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  CHECK(zs::FileIsEqual(local_get_dir + filename, remote_get_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetSmallFile) {
  std::string filename("file_3M");
  OsFileStat file_stat;

  CHECK_EQUAL(0, OsStat(remote_get_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      1, file_stat.length);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  monitor->AppendFile(local_get_dir + filename, remote_get_dir + filename,
                      filename, file_stat.length);

  err_t eno = get_task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  CHECK(zs::FileIsEqual(local_get_dir + filename, remote_get_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetLargeFile) {
  std::string filename("file_3G");
  OsFileStat file_stat;

  CHECK_EQUAL(0, OsStat(remote_get_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      1, file_stat.length);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  monitor->AppendFile(local_get_dir + filename, remote_get_dir + filename,
                      filename, file_stat.length);
  err_t eno = get_task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  CHECK(zs::FileIsEqual(local_get_dir + filename, remote_get_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetSymbolFile) {
  std::string filename("file_symbol");
  OsFileStat file_stat;

  CHECK_EQUAL(0, OsStat(remote_get_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      1, file_stat.length);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  monitor->AppendFile(local_get_dir + filename, remote_get_dir + filename,
                      filename, file_stat.length);
  err_t eno = get_task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  CHECK(zs::FileIsEqual(local_get_dir + filename, remote_get_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetFileInDeepDir) {
  std::string filename("dir1/dir2/dir3/file_3K");
  OsFileStat file_stat;

  CHECK_EQUAL(0, OsStat(remote_get_dir + filename, string(), &file_stat));

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      1, file_stat.length);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  monitor->AppendFile(local_get_dir + filename, remote_get_dir + filename,
                      filename, file_stat.length);
  err_t eno = get_task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  CHECK(zs::FileIsEqual(local_get_dir + filename, remote_get_dir + filename));
}

TEST_FIXTURE(TransferFixture, test_TransferServerGetMultipleFile) {
  CHECK(system("transfer/test_server.sh") != -1);
  err_t eno;
  OsFileStat file_stat;
  int64_t size = 0;

  std::vector<std::string> files = {
    "file_3K",
    "file_3M",
    "file_3G",
    "dir1/dir2/dir3/file_3K",
  };

  for (auto it = files.begin(); it != files.end(); it++) {
    CHECK_EQUAL(0, OsStat(remote_get_dir + *it, string(), &file_stat));
    size += file_stat.length;
  }

  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      4, size);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      monitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  for (auto it = files.begin(); it != files.end(); it++) {
    CHECK_EQUAL(0, OsStat(remote_get_dir + *it, string(), &file_stat));
    monitor->AppendFile(local_get_dir + *it, remote_get_dir + *it,
                        *it, file_stat.length);
    CHECK_EQUAL(
        zs::ZISYNC_SUCCESS, get_task->AppendFile(*it, file_stat.length));
  }

  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;

  CHECK(zs::FileIsEqual(local_get_dir + files[0], remote_get_dir + files[0]));
  CHECK(zs::FileIsEqual(local_get_dir + files[1], remote_get_dir + files[1]));
  CHECK(zs::FileIsEqual(local_get_dir + files[2], remote_get_dir + files[2]));
  CHECK(zs::FileIsEqual(local_get_dir + files[3], remote_get_dir + files[3]));
}


TEST_FIXTURE(TransferFixture, test_TransferServerSetPort) {
  ITransferServer* server = zs::GetTransferServer2();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, server->SetPort(8847)); 
  std::string uri2("tcp://127.0.0.1:8847");
  err_t eno;
  OsFileStat file_stat;
  std::string filename("dir1/dir2/dir3/file_3K");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);
  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task =
      server->CreatePutTask(monitor, 1, "tar", remote_put_tree_uuid, uri2);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, server->SetPort(9525)); 
}

TEST_FIXTURE(TransferFixture, test_TransferServerPutHandlerReturnFail) {
  err_t eno;
  OsFileStat file_stat;
  std::string tree_uuid_error("error");
  std::string filename("file_3K");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);
  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);
  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IPutTask* put_task =
      server_->CreatePutTask(monitor, 0, "tar", tree_uuid_error, uri);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->AppendFile(
      put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;
}

TEST_FIXTURE(TransferFixture, test_TransferServerUploadFile) {
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_3M");
  std::string meta_file("/.zisync.meta");

  std::vector<std::string> files;
  files.push_back(filename);

  int32_t file_length = CreateMetaFile(
      local_put_tree_uuid, remote_put_tree_uuid, put_src_dir, files);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      1, file_stat.length);

  monitor->AppendFile(put_src_dir + filename,
                      put_dst_dir + filename,
                      filename,
                      file_stat.length);

  IUploadTask* upload_task = zs::GetTransferServer2()->CreateUploadTask(
      monitor, 0, "tar/upload", remote_put_tree_uuid, uri);
  CHECK(upload_task != NULL);

  eno = upload_task->AppendFile(
      put_src_dir + meta_file, meta_file, file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = upload_task->AppendFile(
      put_src_dir + filename, filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = upload_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete upload_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}


#include "zisync/kernel/libevent/transfer_server2.h"
#include "zisync/kernel/libevent/tar_put_task.h"
#include "zisync/kernel/libevent/tar_get_task.h"

using zs::TransferServer2;
using zs::TarPutTask2;
using zs::TarGetTask2;

static void BuildPutTask(
    IPutTask* task, std::string put_src_dir, std::string filename) {
  OsFileStat file_stat;
  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  err_t eno = task->AppendFile(put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

}

static void BuildGetTask(
    IGetTask* task, std::string local_get_dir, std::string filename) {
  OsFileStat file_stat;
  CHECK_EQUAL(0, OsStat(local_get_dir + filename, string(), &file_stat));
  err_t eno = task->AppendFile(filename, file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
}

#define NEW_PUT_TASK zs::GetTransferServer2()->CreatePutTask( \
                                                             NULL, 1, "tar", remote_put_tree_uuid, uri) 
#define NEW_GET_TASK zs::GetTransferServer2()->CreateGetTask( \
                                                             NULL, 2, "tar", remote_get_tree_uuid, uri)

TEST_FIXTURE(TransferFixture, test_TransferServerMultipleTask) {
  err_t eno = zs::ZISYNC_SUCCESS;
  std::string meta_file("/.zisync.meta");
  OsFileStat file_stat;
  int64_t size = 0;

  std::vector<std::string> files = {
    "file_empty",
    "file_3K",
    "file_3M",
    "file_3G",
  };

  for (auto it = files.begin(); it != files.end(); it++) {
    CHECK_EQUAL(0, OsStat(put_src_dir + *it, string(), &file_stat));
    size += file_stat.length;
  }

  ITaskMonitor *pmonitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, local_put_tree_uuid, remote_put_tree_uuid,
      4, file_stat.length);
  ITaskMonitor *gmonitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_GET, local_get_tree_uuid, remote_get_tree_uuid,
      4, file_stat.length);

  IPutTask *put_task = zs::GetTransferServer2()->CreatePutTask(
      pmonitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  IGetTask* get_task = zs::GetTransferServer2()->CreateGetTask(
      gmonitor, 2, "tar", remote_get_tree_uuid, uri);
  CHECK(get_task != NULL);

  int32_t file_length = CreateMetaFile(local_put_tree_uuid,
                                       remote_put_tree_uuid,
                                       put_src_dir,
                                       files);
  eno = put_task->AppendFile(
      put_src_dir + meta_file, meta_file, string(), file_length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  for (auto it = files.begin(); it != files.end(); it++) {
    CHECK_EQUAL(0, OsStat(put_src_dir + *it, string(), &file_stat));
    pmonitor->AppendFile(put_src_dir + *it,
                         put_dst_dir + *it,
                         *it,
                         file_stat.length);
    gmonitor->AppendFile(put_src_dir + *it,
                         put_dst_dir + *it,
                         *it,
                         file_stat.length);
    eno = put_task->AppendFile(
        put_src_dir + *it, *it, string(), file_stat.length);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
    eno = get_task->AppendFile(*it, file_stat.length);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  }

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = get_task->Execute(local_get_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  delete put_task;
  delete get_task;
  delete pmonitor;
  delete gmonitor;

  CHECK(zs::FileIsEqual(put_src_dir + files[0], put_dst_dir + files[0]));
  CHECK(zs::FileIsEqual(put_src_dir + files[1], put_dst_dir + files[1]));
  CHECK(zs::FileIsEqual(put_src_dir + files[2], put_dst_dir + files[2]));
  CHECK(zs::FileIsEqual(put_src_dir + files[3], put_dst_dir + files[3]));

  CHECK(zs::FileIsEqual(local_get_dir + files[0], remote_get_dir + files[0]));
  CHECK(zs::FileIsEqual(local_get_dir + files[1], remote_get_dir + files[1]));
  CHECK(zs::FileIsEqual(local_get_dir + files[2], remote_get_dir + files[2]));
  CHECK(zs::FileIsEqual(local_get_dir + files[3], remote_get_dir + files[3]));
};

TEST_FIXTURE(TransferFixture, test_TransferServerCancelTask) {
  //
  // don't using task monitor, otherwise it will failed.
  //
  ((TreeAgent*)tree_agent_)->is_using_task_monitor = false;

  TarPutTask2* put_task0 = (TarPutTask2*)NEW_PUT_TASK;
  TarGetTask2* get_task0 = (TarGetTask2*)NEW_GET_TASK;

  CHECK(put_task0 != NULL);
  CHECK(get_task0 != NULL);

  BuildPutTask(put_task0, put_src_dir, "file_3G");
  BuildGetTask(get_task0, remote_get_dir, "file_3G");

  auto server = (TransferServer2*)zs::GetTransferServer2();
  server->ScheduleTask(put_task0);
  server->ScheduleTask(get_task0);

  server->CancelTask(put_task0->GetTaskId());
  server->CancelTask(get_task0->GetTaskId());

  CHECK(put_task0->Wait() == zs::ZISYNC_ERROR_CANCEL);
  CHECK(get_task0->Wait() == zs::ZISYNC_ERROR_CANCEL);


  delete put_task0;
  delete get_task0;
};

TEST_FIXTURE(TransferFixture, test_PutTransferList) {
  ITaskMonitor *monitor = tree_agent_->CreateTaskMonitor(
      zs::TASK_TYPE_PUT, "src_put", "dst_put", 1, 0);
  err_t eno;
  OsFileStat file_stat;
  std::string filename("file_3G");

  IPutTask* put_task = zs::GetTransferServer2()->CreatePutTask(
      monitor, 0, "tar", remote_put_tree_uuid, uri);
  CHECK(put_task != NULL);

  CHECK_EQUAL(0, OsStat(put_src_dir + filename, string(), &file_stat));
  eno = put_task->AppendFile(put_src_dir + filename, filename, string(), file_stat.length);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;

  CHECK(zs::FileIsEqual(put_src_dir + filename, put_dst_dir + filename));
}
//
//  TEST_FIXTURE(TransferFixture, test_GetTransferList) {
//
//  }
//
//  TEST_FIXTURE(TransferFixture, test_UploadTransferList) {
//
//  }
//
//  TEST_FIXTURE(TransferFixture, test_DownloadTransferList) {
//
//  }
//
//  TEST_FIXTURE(TransferFixture, test_PutHandlerTransferList) {
//
//  }
//
//  TEST_FIXTURE(TransferFixture, test_GetHandlerTransferList) {
//
//  }

using zs::DefaultLogger;

static DefaultLogger logger("./Log");

// #ifndef _MSC_VER

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
  CHECK(system("transfer/test_server.sh") != -1);

  logger.Initialize();
  LogInitialize(&logger);

  // UnitTest::TestReporterStdout reporter;
  zs::TestReporterDoxStdout reporter;
  UnitTest::TestRunner runner(reporter);

  if (argc == 1) {
    // UnitTest::RunAllTests();
    runner.RunTestsIf(UnitTest::Test::GetTestList(), NULL, UnitTest::True(), 0);
  } else if (argc == 2) {
    char *prefix = argv[1];
    Predicate predicate(prefix);
    runner.RunTestsIf(
        UnitTest::Test::GetTestList(), NULL, predicate, 0);
  } else {
    assert(false);
  }
  logger.CleanUp();
  return 0;
}
// #endif 
