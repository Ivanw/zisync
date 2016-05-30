/****************************************************************************
 *       Filename:  test_task.cc
 *
 *    Description:  :
 *
 *        Version:  1.0
 *        Created:  08/29/14 14:53:54
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Wang Wencan 
 *	    Email:  never.wencan@gmail.com
 *        Company:  
 ***************************************************************************/
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"  // NOLINT
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <iostream>
#include <UnitTest++/UnitTest++.h>

#include "zisync/kernel/transfer/tar_put_task.h"
#include "zisync/kernel/transfer/tar_get_task.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/transfer/transfer_server.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/transfer/tar_tree_stat.h"

using zs::err_t;
using zs::ITransferServer;
using zs::IPutTask;
using zs::IGetTask;
using zs::ITreeAgent;
using zs::IPutHandler;
using zs::ISslAgent;
using zs::StringFormat;
using zs::ZmqContext;
using zs::ZmqSocket;
using zs::ZmqMsg;
using zs::OsThread;
using zs::DefaultLogger;
using zs::ITaskMonitor;
using zs::TaskMonitor;
using zs::TreeStat;
using zs::OsFileStat;
using zs::info_t;

const char *format = "tar";

err_t PutTaskRun(ITransferServer *server, const char *filename,
                 const char* uri, const std::string& tree_uuid,
                 TransferInfo* info);
err_t GetTaskRun(ITransferServer *server, const char *filename,
                 const char* uri, const std::string& tree_uuid,
                 const std::string& tmp_dir, TransferInfo* info);

class PutClientThread : public OsThread {
 public:
  PutClientThread(const char* thread_name, ITransferServer* server,
                  const char* file_name, const char* uri, std::string& tree_uuid,
                  TransferInfo* info)
      : OsThread(thread_name) {
        server_ = server;
        file_name_ = file_name;
        uri_ = uri;
        tree_uuid_ = tree_uuid;
        transfer_info_ = info;
      }
  ~PutClientThread() {}

  int Run() {
    err_t eno = PutTaskRun(server_, file_name_, uri_, tree_uuid_, transfer_info_);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

    return eno;
  }

 private:
  ITransferServer* server_;
  const char* file_name_;
  const char* uri_;
  std::string tree_uuid_;
  TransferInfo *transfer_info_;
};

class GetClientThread : public OsThread {
 public:
  GetClientThread(const char* thread_name, ITransferServer* server,
                  const char* file_name, const char* uri,
                  std::string& tree_uuid, const std::string& tmp_dir,
                  TransferInfo* info) :
      OsThread(thread_name) {
        server_ = server;
        file_name_ = file_name;
        uri_ = uri;
        tree_uuid_ = tree_uuid;
        tmp_dir_ = tmp_dir;
        transfer_info_ = info;
      }
  ~GetClientThread() {}

  int Run() {
    err_t eno = GetTaskRun(server_, file_name_,
                           uri_, tree_uuid_, tmp_dir_, transfer_info_);
    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

    return eno;
  }

 private:
  ITransferServer* server_;
  const char* file_name_;
  const char* uri_;
  std::string tree_uuid_;
  std::string tmp_dir_;
  TransferInfo* transfer_info_;
};


class SpeedThread : public OsThread {
 public:
  SpeedThread(const char* thread_name, ITransferServer* server,
              TransferInfo* info, ZmqContext* context) :
      OsThread(thread_name) {
        server_ = server;
        transfer_info_ = info;
        context_ = context;
      }

  ~SpeedThread() {}

  int Run() {
    //    ZmqSocket exit_socket(*context_, ZMQ_SUB);
    //
    //    err_t eno = exit_socket.Connect("inproc://exit");
    //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

    while (1) {
      //      zmq_pollitem_t items[] = {
      //        {exit_socket.socket(), 0, ZMQ_POLLIN, 0},
      //      };
      //      int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), 0);
      //      if (ret >= 0 && items[0].revents & ZMQ_POLLIN) {
      //        return 0;
      //      }
      sleep(1);
      std::vector<info_t*>* list = transfer_info_->GetTransferInfo();
      assert(list != NULL);
      system("clear");
      for (auto it = std::begin(*list); it != std::end(*list); it++) {
        std::cout << "----------------------" << (*it)->tree_uuid_ << "---------------------" << std::endl;
        std::cout << "download: " << std::endl;
        std::cout << "total file: " << (*it)->total_download_file_ << std::endl;
        std::cout << "done file: " << (*it)->current_download_file_ << std::endl;
        std::cout << "total size: " << (*it)->total_download_size_ << std::endl;
        std::cout << "done size: " << (*it)->current_download_size_ << std::endl;
        std::cout << "speed: " << (*it)->download_speed_ << std::endl;
        std::cout << "left time: " << (*it)->download_left_time_ << std::endl;

        std::cout << "upload: " << std::endl;
        std::cout << "total file: " << (*it)->total_upload_file_ << std::endl;
        std::cout << "done file: " << (*it)->current_upload_file_ << std::endl;
        std::cout << "total size: " << (*it)->total_upload_size_ << std::endl;
        std::cout << "done size: " << (*it)->current_upload_size_ << std::endl;
        std::cout << "speed: " << (*it)->upload_speed_ << std::endl;
        std::cout << "left time: " << (*it)->upload_left_time_ << std::endl;
      }
    }
    return 0;
  }

 private:
  ITransferServer* server_;
  TransferInfo *transfer_info_;
  ZmqContext* context_;
};

class TreeAgent : public ITreeAgent {
 public:
  virtual ~TreeAgent() {}

  virtual std::string GetTreeRoot(const std::string& tree_uuid) {
    std::string tree_root;
    if (tree_uuid == "client") {
      tree_root = "transfer/dest/tmp_dir";
    } else if (tree_uuid == "server_get") {
      tree_root = "transfer/src";
    } else if (tree_uuid == "test_error") {
      tree_root = "test_error";
    }
    return tree_root;
  }

  virtual std::string GetNewTmpDir(const std::string& tree_uuid) {
    std::string tmp_dir;
    if (tree_uuid == "client") {
      tmp_dir = "transfer/dest/tmp_dir";
    } else if (tree_uuid == "server_put") {
      tmp_dir = "transfer/dest/server_tmp_dir";
    } else if (tree_uuid == "server_get") {
      tmp_dir = "transfer/dest/server_get_root";
    } else if (tree_uuid == "test_error") {
      tmp_dir = "transfer/dest/test_error";
    }
    return tmp_dir;
  }
};

class PutHandler : public IPutHandler {
 public:
  virtual ~PutHandler() {}

  // virtual err_t OnHandlePut(const std::string& tmp_root) {
  //   if (tmp_root == "transfer/dest/test_error") {
  //     return zs::ZISYNC_ERROR_PUT_FAIL;
  //   }
  //   return zs::ZISYNC_SUCCESS;
  // }

  virtual bool StartupHandlePut(
      const std::string& tmp_root, int32_t *task_id) {
    if (tmp_root == "transfer/dest/test_error") {
      return false;
    }
    return true;
  }

  virtual bool OnHandleFile(
      int32_t task_id, const std::string& relative_path, 
      const std::string& sha1) {
    return true;
  }
  
  virtual bool ShutdownHandlePut(int32_t task_id) {
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


int CompareFile(const char *expected, const char *actual) {
  printf("expected = %s\n", expected);
  printf("actual = %s\n", actual);
  FILE *actual_fp = NULL;
  FILE *expected_fp = NULL;
  char actual_buf[1024] = {0};
  char expected_buf[1024] = {0};

  actual_fp = zs::OsFopen(actual, "r");
  perror("fopen");
  CHECK(actual_fp != NULL);
  expected_fp = zs::OsFopen(expected, "r");
  CHECK(expected_fp != NULL);

  while (1) {
    fread(actual_buf, 1, 1024, actual_fp);
    size_t nbytes_expected = fread(expected_buf, 1, 1024, expected_fp);

    if (nbytes_expected == 0) {
      break;
    }

    if (memcmp(actual_buf, expected_buf, nbytes_expected)) {
      return -1;
    }
  }
  fclose(actual_fp);
  fclose(expected_fp);

  return 0;
}

err_t GetTaskRun(
    ITransferServer *server, const char *filename,
    const char* uri, const std::string& tree_uuid,
    const std::string& tmp_dir, TransferInfo *info) {
  printf("-----------------------------------------------Get Test-----------------------------------------\n");
  TreeStat stat;
  ITaskMonitor *monitor = new TaskMonitor(&stat, 0, 0);
  assert(monitor != NULL);
  std::string method("GET");
  info->AppendTreeStat(tree_uuid, method, &stat);
  IGetTask *get_task = server->CreateGetTask(format, tree_uuid, uri, monitor);
  CHECK(get_task != NULL);

  err_t eno = zs::ZISYNC_SUCCESS;
  eno = get_task->AppendFile(filename);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  eno = get_task->Execute(tmp_dir);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete get_task;
  delete monitor;
  info->DeleteTreeStat(tree_uuid, method, &stat);
  return eno;
}

err_t PutTaskRun(ITransferServer *server, const char *filename,
                 const char* uri, const std::string& tree_uuid,
                 TransferInfo *info) {
  printf("-----------------------------------------------Put Test-----------------------------------------\n");

  std::string buffer;
  StringFormat(&buffer, "transfer/src/%s", filename);
  err_t eno = zs::ZISYNC_SUCCESS;
  //get filesize
  OsFileStat file_stat;  
  CHECK_EQUAL(0, OsStat(filename, &file_stat));
  TreeStat stat;
  ITaskMonitor *monitor = new TaskMonitor(&stat, 1, file_stat.length);
  assert(monitor != NULL);
  std::string method("PUT");
  info->AppendTreeStat(tree_uuid, method, &stat);
  IPutTask *put_task = server->CreatePutTask(format, tree_uuid, uri, monitor);
  CHECK(put_task != NULL);

  eno = put_task->AppendFile(buffer, filename);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  eno = put_task->Execute();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  delete put_task;
  delete monitor;
  info->DeleteTreeStat(tree_uuid, method, &stat);
  return eno;
}

TEST(test_TransferServer) {
  CHECK(system("transfer/transfer.sh") != -1);

  ITransferServer *server = zs::GetTransferServer();
  CHECK(server != NULL);

  ITreeAgent *tree_agent = new TreeAgent;
  IPutHandler *put_handler = new PutHandler;
  ISslAgent* ssl_agent = new SslAgent;

  ZmqContext context;
  ZmqSocket test_socket(context, ZMQ_PUB);
  err_t eno = test_socket.Bind("inproc://exit");
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);

  TransferInfo transfer_info;
//  SpeedThread speed_thread("Speed", server, &transfer_info, &context);
//  speed_thread.Startup();
  server->Startup(8848, put_handler, tree_agent, ssl_agent, &context);

  //put
  std::string tree_uuid("client");
  //  //test more file
  //  {
  //    //printf("-----------------------------------------------Put More files Test-----------------------------------------\n");
  //    IPutTask *put_task = server->CreatePutTask(format, tree_uuid,
  //                                               "file://transfer/dest/1_more");
  //    CHECK(put_task != NULL);
  //
  //    eno = put_task->AppendFile("transfer/src/1", "1");
  //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  //    eno = put_task->AppendFile("transfer/src/file", "file");
  //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  //    eno = put_task->AppendFile("transfer/src/file3", "file3");
  //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  //    eno = put_task->AppendFile("transfer/src/empty_dir", "empty_dir");
  //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  //    eno = put_task->Execute();
  //    CHECK_EQUAL(zs::ZISYNC_SUCCESS, eno);
  //    delete put_task;
  //
  //    CHECK_EQUAL(0, CompareFile("transfer/dest/1_more", "transfer/dest/expected/1_more_expected"));
  //  }

  //get
  std::string tmp_dir("transfer/dest/tmp_dir");
  //  //test small file
  //  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
  //              GetTaskRun(server, "file", "file://transfer/dest/tmp_file_get", tree_uuid, tmp_dir));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_file_get",
  //                             "transfer/dest/expected/get_send_dir/file_expected"));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_dir/file",
  //                             "transfer/dest/expected/tmp_dir/file"));
  //  //test big file
  //  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
  //              GetTaskRun(server, "file3", "file://transfer/dest/tmp_file3_get", tree_uuid, tmp_dir));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_file3_get",
  //                             "transfer/dest/expected/get_send_dir/file3_expected"));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_dir/file3",
  //                             "transfer/dest/expected/tmp_dir/file3"));
  //  //test empty dir
  //  CHECK_EQUAL(zs::ZISYNC_SUCCESS,
  //              GetTaskRun(server, "empty_dir",
  //                         "file://transfer/dest/tmp_empty_dir_get", tree_uuid, tmp_dir));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_empty_dir_get",
  //                             "transfer/dest/expected/get_send_dir/empty_dir_expected"));
  //  CHECK_EQUAL(0, CompareFile("transfer/dest/tmp_dir/empty_dir",
  //                             "transfer/dest/expected/tmp_dir/empty_dir"));

//  //more threads test
//  tree_uuid = "server_put";
//  PutClientThread put1("put1", server, "1", "tcp://127.0.0.1:8848", tree_uuid, &transfer_info);
//  PutClientThread put2("put2", server, "file", "tcp://127.0.0.1:8848", tree_uuid, &transfer_info);
//  PutClientThread put3("put3", server, "file3", "tcp://127.0.0.1:8848", tree_uuid, &transfer_info);
//  PutClientThread put4("put4", server, "empty_dir", "tcp://127.0.0.1:8848", tree_uuid, &transfer_info);
//
//  tree_uuid = "server_get";
//  tmp_dir = "transfer/dest/server_get_root";
//  GetClientThread get1("get1", server, "1", "tcp://127.0.0.1:8848", tree_uuid, tmp_dir, &transfer_info);
//  GetClientThread get2("get2", server, "file", "tcp://127.0.0.1:8848", tree_uuid, tmp_dir, &transfer_info);
//  GetClientThread get3("get3", server, "file3", "tcp://127.0.0.1:8848", tree_uuid, tmp_dir, &transfer_info);
//  GetClientThread get4("get4", server, "empty_dir", "tcp://127.0.0.1:8848", tree_uuid, tmp_dir, &transfer_info);
//
//  CHECK_EQUAL(0, put1.Startup());
//  CHECK_EQUAL(0, put2.Startup());
//  CHECK_EQUAL(0, put3.Startup());
//  CHECK_EQUAL(0, put4.Startup());
//
//  CHECK_EQUAL(0, get1.Startup());
//  CHECK_EQUAL(0, get2.Startup());
//  CHECK_EQUAL(0, get3.Startup());
//  CHECK_EQUAL(0, get4.Startup());
//
//  std::cout << "----------------------------------------test1---------------------------------" << std::endl;
//  CHECK_EQUAL(0, put1.Shutdown());
//  std::cout << "----------------------------exit 1-------------------------" << std::endl;
//  CHECK_EQUAL(0, put2.Shutdown());
//  std::cout << "----------------------------exit 2-------------------------" << std::endl;
//  CHECK_EQUAL(0, put3.Shutdown());
//  std::cout << "----------------------------exit 3-------------------------" << std::endl;
//  CHECK_EQUAL(0, put4.Shutdown());
//  std::cout << "----------------------------exit 4-------------------------" << std::endl;
//  std::cout << "----------------------------------------test2---------------------------------" << std::endl;
//
//  CHECK_EQUAL(0, get1.Shutdown());
//  std::cout << "----------------------------exit 5-------------------------" << std::endl;
//  CHECK_EQUAL(0, get2.Shutdown());
//  std::cout << "----------------------------exit 6-------------------------" << std::endl;
//  CHECK_EQUAL(0, get3.Shutdown());
//  std::cout << "----------------------------exit 7-------------------------" << std::endl;
//  CHECK_EQUAL(0, get4.Shutdown());
//  std::cout << "----------------------------exit 8-------------------------" << std::endl;
//  std::cout << "----------------------------------------test3---------------------------------" << std::endl;
//
//  CHECK_EQUAL(0, CompareFile("transfer/dest/server_tmp_dir/1", "transfer/dest/expected/server_tmp_dir/1"));
//  CHECK_EQUAL(0, CompareFile("transfer/dest/server_tmp_dir/file", "transfer/dest/expected/server_tmp_dir/file"));
//  CHECK_EQUAL(0, CompareFile("transfer/dest/server_tmp_dir/file3", "transfer/dest/expected/server_tmp_dir/file3"));
//  CHECK_EQUAL(0, CompareFile("transfer/dest/server_tmp_dir/empty_dir",
//                             "transfer/dest/expected/server_tmp_dir/empty_dir"));
//
//  CHECK_EQUAL(0, CompareFile("transfer/src/1/2/3/4/5/file2",
//                             "transfer/dest/server_get_root/1/2/3/4/5/file2"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/1/2/file1",
//                             "transfer/dest/server_get_root/1/2/file1"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/1/file1",
//                             "transfer/dest/server_get_root/1/file1"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/1/file2",
//                             "transfer/dest/server_get_root/1/file2"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/1/file3",
//                             "transfer/dest/server_get_root/1/file3"));
//
//  CHECK_EQUAL(0, CompareFile("transfer/src/file", "transfer/dest/server_get_root/file"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/file3", "transfer/dest/server_get_root/file3"));
//  CHECK_EQUAL(0, CompareFile("transfer/src/empty_dir",
//                             "transfer/dest/server_get_root/empty_dir"));
//
  std::cout << "----------------------------------------test4---------------------------------" << std::endl;
  //exit
  ZmqMsg exit_msg;
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, exit_msg.SendTo(test_socket, 0));
  server->Shutdown();

  delete tree_agent;
  delete put_handler;
  delete ssl_agent;
  std::cout << "-----------------------------------end---------------------------" << std::endl;
}

static DefaultLogger logger("./Log");

int main(int /* argc */, char** /* argv */) {
  logger.Initialize();
  logger.error_to_stderr = true;
  LogInitialize(&logger);

  UnitTest::RunAllTests();
  return 0;
}
