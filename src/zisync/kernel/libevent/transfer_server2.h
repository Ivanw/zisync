/**
 * @file transfer.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Transfer implementation.
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
 * This source code may be translated into executable form and incorporated * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#ifndef ZISYNC_KERNEL_TRANSFER_TRANSFER_SERVER_H_
#define ZISYNC_KERNEL_TRANSFER_TRANSFER_SERVER_H_

#include <openssl/ssl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/libevent/transfer.h"

struct evconnlistener;

namespace zs {

class TransferConnection;
class TransferTask;

static const int g_socket_buffer_size = 1024 * 1024;
static const int g_file_buffer_size = 4 * 1024;

class TransferServer2 : public ITransferServer
                      , public IListenEventDelegate {

  friend ITransferServer* GetTransferServer2();

 public:
  TransferServer2();
  virtual ~TransferServer2();

  //
  // Implement ILibEventVirtualServer
  //
  virtual err_t Startup(ILibEventBase* base);
  virtual err_t Shutdown(ILibEventBase* base);
  virtual ILibEventBase* evbase() { return evbase_; }

  //
  // Implement IListenEventDelegate
  virtual void OnAccept(struct evconnlistener *listener,
                        evutil_socket_t socket,
                        struct sockaddr *sockaddr, int socklen);
  //
  // Implement ITransferServer
  //
  virtual err_t Initialize(
      int32_t port,
      ITreeAgent* tree_agent,
      ISslAgent* ssl_agent);
  virtual err_t CleanUp();
  
  virtual IGetTask* CreateGetTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri);
  
  virtual IDownloadTask* CreateDownloadTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri);

  virtual IPutTask* CreatePutTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri);
  
  virtual IUploadTask* CreateUploadTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri);

  virtual ITreeAgent* GetTreeAgent();

  virtual err_t CancelTask(int task_id);
  virtual err_t CancelTaskByTree(int32_t tree_id);

  virtual err_t QueryTaskInfoResult(QueryTransferTaskInfoResult *result);

  virtual err_t SetPort(int32_t port);

  virtual err_t ScheduleTask(TransferTask* task);

 public:
  err_t ChangeListenPort(int32_t port);
  void RemoveConnect(int32_t task_id);
  void RemoveTask(int32_t task_id);

 private:
  static void LambdaCancleTask(void* ctx);
  static void LambdaCancleTaskByTree(void* ctx);
  static void LambdaSchduleTask(void* ctx);

  int32_t port_;
  ITreeAgent*  tree_agent_;

  ILibEventBase* evbase_;
  struct evconnlistener* listener_;

  AtomicInt32 next_task_id_;
  std::unordered_map<int, std::shared_ptr<TransferConnection>> conn_map_;
  std::unordered_map<int, TransferTask*> task_map_;
  
  static TransferServer2 s_instance;
};

class PutTaskFactory2 {
  friend class TransferServer2;

 public:
  PutTaskFactory2() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~PutTaskFactory2() { /* virtual destructor */ }

  virtual IPutTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  PutTaskFactory2* GetNextFactory() {
    return next_factory_;
  }

 protected:
  PutTaskFactory2* next_factory_;
  static PutTaskFactory2* s_first_factory_;
};

class GetTaskFactory2 {
  friend class TransferServer2;

 public:
  GetTaskFactory2() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~GetTaskFactory2() { /* virtual destructor */ }

  virtual IGetTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  GetTaskFactory2* GetNextFactory() {
    return next_factory_;
  }

 protected:
  GetTaskFactory2* next_factory_;
  static GetTaskFactory2* s_first_factory_;
};

class DownloadTaskFactory2 {
  friend class TransferServer2;

 public:
  DownloadTaskFactory2() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~DownloadTaskFactory2() { /* virtual destructor */ }

  virtual IDownloadTask* CreateTask(
      ITaskMonitor* monitor,
      int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  DownloadTaskFactory2* GetNextFactory() {
    return next_factory_;
  }

 protected:
  DownloadTaskFactory2* next_factory_;
  static DownloadTaskFactory2* s_first_factory_;
};


class ITaskHandler2 {
 public:
  virtual ~ITaskHandler2() {
    /* virtual destructor */
  }

  virtual err_t OnHandleTask(
      IAbortable* abortable,
      const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid,
      std::istream& in, std::ostream& out,
      ITaskMonitor* monitor) = 0;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TRANSFER_SERVER_H_
