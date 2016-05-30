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
#include <map>

#include "zisync/kernel/transfer/transfer.h"

namespace zs {

class IGetTask;
class IPutTask;
class IPutHandler;
class ListenThread;
class WorkerThread;
class ZmqSocket;
class ZmqContext;
class ITaskHandler;
class IAbortable;
class OsTcpSocket;
class OsMutex;
class TasksManager;

static const int g_socket_buffer_size = 1024 * 1024;
static const int g_file_buffer_size = 4 * 1024;

class TransferServer : public ITransferServer {
  friend ITransferServer* GetTransferServer();

 public:
  TransferServer();
  virtual ~TransferServer();

  virtual err_t Initialize(
      int32_t port,
      IPutHandler* put_handler,
      ITreeAgent* tree_agent,
      ISslAgent* ssl_agent);
  virtual err_t CleanUp();

  /*
   * task_format can be "tar", "tgz", "dif"
   * uri should be "host:port"
   */
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
      const char* task_format,
      const std::string& remote_tree_uuid,
      const std::string& uri) { return NULL; }

  virtual ITaskHandler* CreateTaskHandler(
      const std::string& method,
      const std::string& format);

  virtual ITreeAgent* GetTreeAgent();
  virtual IPutHandler* GetPutHandler() { return put_handler_; }

  virtual err_t CancelTask(int task_id);
  virtual err_t CancelAllTask();

  virtual err_t QueryTaskInfoResult(QueryTransferTaskInfoResult *result);

  virtual err_t SetPort(int32_t port);

  int ListenRun();
  void DeleteWorkerThread(WorkerThread* woker_thread);

 private:
  err_t HandleSetPort(int32_t new_port);

  IPutHandler* put_handler_;
  ITreeAgent*  tree_agent_;

  ListenThread* listen_thread_;
  OsTcpSocket* listen_socket_;
  ZmqSocket* exit_socket_;
  ZmqSocket* cmd_socket_router_;
  SSL_CTX* ctx_;
  std::vector<WorkerThread*> worker_threads_;
  static TransferServer s_instance;
};

class PutTaskFactory {
  friend class TransferServer;

 public:
  PutTaskFactory() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~PutTaskFactory() { /* virtual destructor */ }

  virtual IPutTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  PutTaskFactory* GetNextFactory() {
    return next_factory_;
  }

 protected:
  PutTaskFactory* next_factory_;
  static PutTaskFactory* s_first_factory_;
};

class GetTaskFactory {
  friend class TransferServer;

 public:
  GetTaskFactory() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~GetTaskFactory() { /* virtual destructor */ }

  virtual IGetTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  GetTaskFactory* GetNextFactory() {
    return next_factory_;
  }

 protected:
  GetTaskFactory* next_factory_;
  static GetTaskFactory* s_first_factory_;
};

class DownloadTaskFactory {
  friend class TransferServer;

 public:
  DownloadTaskFactory() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~DownloadTaskFactory() { /* virtual destructor */ }

  virtual IDownloadTask* CreateTask(
      ITaskMonitor* monitor,
      int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx) = 0;
  virtual const std::string& GetFormat() = 0;

  DownloadTaskFactory* GetNextFactory() {
    return next_factory_;
  }

 protected:
  DownloadTaskFactory* next_factory_;
  static DownloadTaskFactory* s_first_factory_;
};


class ITaskHandler {
 public:
  virtual ~ITaskHandler() {
    /* virtual destructor */
  }

  virtual err_t OnHandleTask(
      IAbortable* abortable,
      const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid,
      std::istream& in, std::ostream& out,
      ITaskMonitor* monitor) = 0;
};


class TaskHandlerFactory {
  friend class TransferServer;

 public:
  TaskHandlerFactory() {
    next_factory_ = s_first_factory_;
    s_first_factory_ = this;
  }
  virtual ~TaskHandlerFactory() { /* virtual destructor */ }

  virtual ITaskHandler* CreateTaskHandler() = 0;
  virtual const std::string& GetFormat() = 0;
  virtual const std::string& GetMethod() = 0;

  TaskHandlerFactory* GetNextFactory() {
    return next_factory_;
  }

 protected:
  TaskHandlerFactory* next_factory_;
  static TaskHandlerFactory* s_first_factory_;
};

class TasksManager {
  friend TasksManager* GetTasksManager();
 public:
  TasksManager();
  ~TasksManager();
  void SetTaskAbort(int32_t task_id);
  void CancelAllTask();
  bool TaskAbort(int32_t task_id);
  int32_t AddTask();
  void ClearTaskAbort() {
    cancel_all_task_ = false;
  }

 private:
  int32_t ids_;
  bool cancel_all_task_;
  std::vector<int32_t> tasks_;
  OsMutex *mutex_;
  static TasksManager s_instance;
};

TasksManager* GetTasksManager();

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TRANSFER_SERVER_H_
