/**
 * @file transfer_server.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Transfer server implmentation.
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
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/transfer/transfer_server.h"

#include <stdlib.h>
#include <string.h>
#include <libtar.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <memory>
#include <iostream>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "zisync/kernel/transfer/transfer.pb.h"
#pragma warning(pop)
#else
#include "zisync/kernel/transfer/transfer.pb.h"
#endif

#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/transfer/tar_put_task.h"
#include "zisync/kernel/transfer/tar_get_task.h"
#include "zisync/kernel/transfer/tar_download_task.h"
#include "zisync/kernel/transfer/tar_put_handler.h"
#include "zisync/kernel/transfer/tar_get_handler.h"
#include "zisync/kernel/status.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/format.h"
#include "zisync/kernel/utils/context.h"

namespace zs {
using std::unique_ptr;

static const char* cmd_uri = "inproc://transfer_cmd";

class WorkerThread : public OsThread, public IAbortable {
 public:
  WorkerThread(const char* thread_name, TransferServer* server,
               OsTcpSocket* connect_socket, SSL_CTX* ctx);
  virtual ~WorkerThread();
  virtual int Run();

  int ReceiveAndProcessData();

  virtual bool Abort() {
    aborted_ = true;
    return true;
  }
  virtual bool IsAborted() {
    return aborted_;
  }


 private:
  WorkerThread(WorkerThread&);
  void operator=(WorkerThread&);

  TransferServer* server_;
  OsTcpSocket* connect_socket_;
  SSL_CTX* ctx_;
  bool aborted_;
};

class ListenThread : public OsThread {
 public:
  ListenThread(const char* thread_name, TransferServer* server);
  virtual ~ListenThread();
  virtual int Run();

 private:
  ListenThread(ListenThread&);
  void operator=(ListenThread&);

  TransferServer* server_;
};

TasksManager TasksManager::s_instance;

void TasksManager::SetTaskAbort(int32_t task_id) {
  MutexAuto auto_mutex(mutex_);
  auto it = std::find(tasks_.begin(), tasks_.end(), task_id);
  if (it != tasks_.end()) {
    tasks_.erase(it);
  }
}

void TasksManager::CancelAllTask() {
  MutexAuto auto_mutex(mutex_);
  cancel_all_task_ = true;
  tasks_.clear();
}

bool TasksManager::TaskAbort(int32_t task_id) {
  MutexAuto auto_mutex(mutex_);
  if (std::find(tasks_.begin(), tasks_.end(), task_id) == tasks_.end() ||
      cancel_all_task_ == true) {
    return true;
  } else {
    return false;
  }
}

int32_t TasksManager::AddTask() {
  MutexAuto auto_mutex(mutex_);
  tasks_.push_back(ids_);
  return ids_++;
}

TasksManager* GetTasksManager() {
  return &TasksManager::s_instance;
}

TransferServer TransferServer::s_instance;

ITransferServer* GetTransferServer() {
  return &TransferServer::s_instance;
}

/* class ListenThread */
ListenThread::ListenThread(
    const char* thread_name, TransferServer* server) : OsThread(thread_name) {
  server_ = server;
}

ListenThread::~ListenThread() {
}

int ListenThread::Run() {
  return server_->ListenRun();
}

/* class WorkerThread */
WorkerThread::WorkerThread(
    const char* thread_name, TransferServer* server,
    OsTcpSocket* connect_socket, SSL_CTX* ctx)
  : OsThread(thread_name) {
    aborted_ = false;
    server_ = server;
    connect_socket_ = connect_socket;
    ctx_ = ctx;
  }

WorkerThread::~WorkerThread() {
  if (connect_socket_ != NULL) {
    delete connect_socket_;
    connect_socket_ = NULL;
  }
}

static inline void SendErrorResponse(std::ostream& out, int error_code) {
  switch (error_code) {
    case 400:
      out.write("HTTP/1.1 400 Bad Request\r\n\r\n", 28);
      break;
    case 401:
      out.write("HTTP/1.1 401 unsupported\r\n\r\n", 28);
      break;
    case 500:
      out.write("HTTP/1.1 500 Internal Server Error\r\n\r\n", 28);
      break;
    default:
      ZSLOG_ERROR("Set invalid http error code.");
  }
}

int WorkerThread::Run() {
  int rc = ReceiveAndProcessData();

  // CleanUp myself from TransferServer
  server_->DeleteWorkerThread(this);
  delete this;

  return rc;
}
int WorkerThread::ReceiveAndProcessData() {
  fdbuf fd_buf(connect_socket_);
  std::istream in(&fd_buf);
  std::ostream out(&fd_buf);

  char method[4] = {0};
  char format[4] = {0};
  std::string line;
  // parser method and format from first line.
  std::getline(in, line, '\n');
  if (in.fail()) {
    ZSLOG_ERROR("Read method and format fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  int ret = sscanf(line.c_str(), "%4[^ ] %4s", method, format);
  assert(ret > 0);

  // parse head for tree_uuid, total_size, total_files
  char key[30] = {0};
  char val[256] = {0};
  char remote_tree_uuid[40] = {0};
  char local_tree_uuid[40] = {0};
  int64_t total_size = 0;
  int32_t total_files = 0;
  int count = 0;
  while (1) {
    std::getline(in, line, '\n');
    if (line == "\r" || in.fail()) {
      break;
    }

    ret = sscanf(line.c_str(), "%30[^:]:%256s", key, val);
    assert(ret > 0);
    if (memcmp(key, "ZiSync-Remote-Tree-Uuid", 23) == 0 ||
        memcmp(key, "ZiSync-Tree-Uuid", 16) == 0) {
      strncpy(local_tree_uuid, val, 40);
      count++;
    } else if (memcmp(key, "ZiSync-Local-Tree-Uuid", 22) == 0) {
      strncpy(remote_tree_uuid, val, 40);
      count++;
    } else if (memcmp(key, "ZiSync-Total-Size", 17) == 0) {
      ret = sscanf(val, "%" PRId64, &total_size);
      assert(ret > 0);
      count++;
    } else if (memcmp(key, "ZiSync-Total-Files", 18) == 0) {
      ret = sscanf(val, "%d", &total_files);
      assert(ret > 0);
      count++;
    }
  }

  ITreeAgent* tree_agent = server_->GetTreeAgent();
  assert(tree_agent != NULL);
  int32_t remote_tree_id = -1;
  int32_t local_tree_id = -1;
  int new_value = g_socket_buffer_size;
  StatusType type;

  local_tree_id = tree_agent->GetTreeId(local_tree_uuid);
  if (local_tree_id == -1) {
    ZSLOG_ERROR("Have no tree(%s).", local_tree_uuid);
    return ZISYNC_ERROR_TREE_NOENT;
  }
  bool need_unlock = false;
  if (strcmp(remote_tree_uuid, "") != 0) {
    remote_tree_id = tree_agent->GetTreeId(remote_tree_uuid);
    if (remote_tree_id == -1) {
      ZSLOG_ERROR("Have no tree(%s).", remote_tree_uuid);
      return ZISYNC_ERROR_TREE_NOENT;
    }
  }
  if (memcmp(method, "PUT", 3) == 0) {   // for version 1.3.0
    if (remote_tree_id != -1) {
      assert(local_tree_id != -1);
      if (tree_agent->TryLock(local_tree_id, remote_tree_id) == false) {
        ZSLOG_WARNING("TryLock fail(local_tree: %s, remote_tree: %s)",
                      local_tree_uuid, remote_tree_uuid);
        return ZISYNC_ERROR_GENERAL;
      }
      need_unlock = true;
    }

    type = ST_GET;
    if (connect_socket_->SetSockOpt(
            SOL_SOCKET, SO_RCVBUF, &new_value, sizeof(int)) == -1) {
      ZSLOG_ERROR("Set socket recv buf length(256K) fail: %s", OsGetLastErr());
    }
  } else if (memcmp(method, "GET", 3) == 0) {
    type = ST_PUT;
    if (connect_socket_->SetSockOpt(
            SOL_SOCKET, SO_SNDBUF, &new_value, sizeof(int)) == -1) {
      ZSLOG_ERROR("Set socket send buf length(256K) fail: %s", OsGetLastErr());
    }
  } else {
    SendErrorResponse(out, 400);
    ZSLOG_ERROR("Transfer Worker recive http header fail.");
    return ZISYNC_ERROR_INVALID_MSG;
  }

  err_t zisync_ret = ZISYNC_SUCCESS;

  ZSLOG_INFO(
      "%s files[%d: %s] from %s to %s by format %s", method, total_files,
      HumanFileSize(total_size), remote_tree_uuid, local_tree_uuid, format);

  ITaskHandler* handler = server_->CreateTaskHandler(method, format);
  assert(handler != NULL);
  if (handler != NULL) {
    int32_t sync_id = tree_agent->GetSyncId(local_tree_uuid);
    unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id));
    assert(sync);
    zs::GetEventNotifier()->NotifySyncStart(sync_id, ToExternalSyncType(sync->type()));
    TaskMonitor monitor(
        local_tree_id, remote_tree_id, type, total_files, total_size);
    zisync_ret = handler->OnHandleTask(
        this, local_tree_uuid, remote_tree_uuid, in, out, &monitor);
    zs::GetEventNotifier()->NotifySyncFinish(sync_id, ToExternalSyncType(sync->type()));
    delete handler;
  } else {
    zisync_ret = ZISYNC_ERROR_INVALID_MSG;
  }
  if (need_unlock) {
    tree_agent->Unlock(local_tree_id, remote_tree_id);
  }

  // send response
  if (memcmp(method, "PUT", 3) == 0) {
    switch (zisync_ret) {
      case  ZISYNC_SUCCESS:
        out.write("HTTP/1.1 200 OK\r\n\r\n", 19);
        break;
      case ZISYNC_ERROR_INVALID_MSG:
        SendErrorResponse(out, 401);
        break;
      case ZISYNC_ERROR_TREE_NOENT:
      case ZISYNC_ERROR_GETTREEROOT:
        SendErrorResponse(out, 400);
        break;
      case ZISYNC_ERROR_GENERAL:
      case ZISYNC_ERROR_TAR:
      case ZISYNC_ERROR_CANCEL:
      default:
        SendErrorResponse(out, 500);
    }
  }
  out.flush();

  return zisync_ret;
}

/* class TransferServer */
TransferServer::TransferServer() {
  listen_thread_ = NULL;
  listen_socket_ = NULL;
  exit_socket_ = NULL;
  put_handler_ = NULL;
  tree_agent_ = NULL;
  ctx_ = NULL;
}

TransferServer::~TransferServer() {
  CleanUp();
  assert(listen_thread_ == NULL);
  assert(listen_socket_ == NULL);
  assert(exit_socket_ == NULL);
  assert(ctx_ == NULL);
}

void TransferServer::DeleteWorkerThread(WorkerThread* worker_thread) {
  auto it = std::find(
      worker_threads_.begin(), worker_threads_.end(), worker_thread);
  if (it != worker_threads_.end()) {
    worker_threads_.erase(it);
  }
}

err_t TransferServer::HandleSetPort(int32_t new_port) {
  std::string addr;
  int ret = StringFormat(&addr, "tcp://*:%d", new_port);
  assert(ret > 0);

  unique_ptr<OsTcpSocket> socket(OsTcpSocketFactory::Create(addr));
  assert(socket != NULL);

  ret = socket->Bind();
  if (ret == EADDRINUSE) {
    ZSLOG_ERROR("port(%d) has been used.", listen_socket_->fd());
    return ZISYNC_ERROR_ADDRINUSE;
  } else if (ret != 0) {
    ZSLOG_ERROR("tcp_socket_.Bind(%s) fail: %s", addr.c_str(), OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  ret = socket->Listen(20);
  if (ret == -1) {
    ZSLOG_ERROR("Transfer HandleSetPort Listen fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  listen_socket_->Swap(socket.get());

  return ZISYNC_SUCCESS;
}

err_t TransferServer::SetPort(int32_t port) {
  ZmqSocket cmd_socket_req(GetGlobalContext(), ZMQ_REQ);
  err_t zisync_ret = cmd_socket_req.Connect(cmd_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connect to cmd(%s) fail: %s",
                cmd_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  ZmqMsg request;
  request.SetData(&port, sizeof(port));
  zisync_ret = request.SendTo(cmd_socket_req, 0);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send request to %s fail: %s",
                cmd_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  ZmqMsg response;
  zisync_ret = response.RecvFrom(cmd_socket_req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Recv request from %s fail: %s",
                cmd_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  return *static_cast<const err_t*>(response.data());
}

err_t TransferServer::Initialize(
    int32_t port, IPutHandler* put_handler, ITreeAgent* tree_agent,
    ISslAgent* ssl_agent) {
  assert(put_handler != NULL);
  assert(tree_agent != NULL);
  put_handler_ = put_handler;
  tree_agent_ = tree_agent;
  // TasksManager init
  GetTasksManager()->ClearTaskAbort();
  // SSL
  SSL_library_init();
  SSL_load_error_strings();

  // listen_socket
  std::string uri;
  int ret = StringFormat(&uri, "tcp://*:%d", port);
  assert(ret > 0);

  if (ssl_agent == NULL) {
    listen_socket_ = OsTcpSocketFactory::Create(uri);
    assert(listen_socket_ != NULL);
    ctx_ = NULL;
  } else {
    const SSL_METHOD* meth = SSLv3_method();
    if (meth == NULL) {
      ZSLOG_ERROR("Initialize ssl method fail.");
      return ZISYNC_ERROR_SSL;
    }

    ctx_ = SSL_CTX_new(meth);
    if (ctx_ == NULL) {
      ZSLOG_ERROR("Create SSL_CTX fail: %s.",
                  ERR_error_string(ERR_get_error(), NULL));
      return ZISYNC_ERROR_SSL;
    }

    std::string certificate_path = ssl_agent->GetCertificate();
    if (certificate_path.empty()) {
      ZSLOG_ERROR("Get certificate path fail.");
      return ZISYNC_ERROR_GET_CERTIFICATE;
    }

    std::string ca_certificate_path = ssl_agent->GetCaCertificate();
    if (ca_certificate_path.empty()) {
      ZSLOG_ERROR("Get ca certificate path fail.");
      return ZISYNC_ERROR_GET_CA;
    }

    std::string private_key = ssl_agent->GetPrivateKey();
    if (private_key.empty()) {
      ZSLOG_ERROR("Get private key path fail.");
      return ZISYNC_ERROR_GET_PRIVATE_KEY;
    }

    if (SSL_CTX_use_certificate_file(
            ctx_, certificate_path.c_str(), SSL_FILETYPE_PEM) != 1) {
      ZSLOG_ERROR("Load certificate file fail: %s.",
                  ERR_error_string(ERR_get_error(), NULL));
      return ZISYNC_ERROR_SSL;
    }

    if (SSL_CTX_use_PrivateKey_file(
            ctx_, private_key.c_str(), SSL_FILETYPE_PEM) != 1) {
      ZSLOG_ERROR("Load private key file fail: %s.",
                  ERR_error_string(ERR_get_error(), NULL));
      return ZISYNC_ERROR_SSL;
    }

    if (SSL_CTX_check_private_key(ctx_) != 1) {
      ZSLOG_ERROR("Private key does not match the certificate public key: %s.",
                  ERR_error_string(ERR_get_error(), NULL));
      return ZISYNC_ERROR_PRIVATE_KEY_CHECK;
    }

    if (SSL_CTX_load_verify_locations(ctx_, ca_certificate_path.c_str(), NULL)
        == 0) {
      ZSLOG_ERROR("Load CA certificate file fail: %s.",
                  ERR_error_string(ERR_get_error(), NULL));
      return ZISYNC_ERROR_SSL;
    }

    SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, NULL);  //???
    SSL_CTX_set_verify_depth(ctx_, 1);  //???
    SSL_CTX_set_quiet_shutdown(ctx_, 1);

    listen_socket_ = OsTcpSocketFactory::Create(uri, ctx_);
    assert(listen_socket_ != NULL);
  }

  if ((ret = listen_socket_->Bind()) != 0) {
    ZSLOG_ERROR("Transfer listen thread bind fail: %s", OsGetLastErr());
    if (ret == EADDRINUSE) {
      return ZISYNC_ERROR_ADDRINUSE;
    } else {
      return ZISYNC_ERROR_OS_SOCKET;
    }
  }

  if (listen_socket_->Listen(20) != 0) {
    ZSLOG_ERROR("Transfer listen thread listen fail: %s", OsGetLastErr());
    return ZISYNC_ERROR_OS_SOCKET;
  }

  // exit_socket
  const ZmqContext &context = GetGlobalContext();
  exit_socket_ = new ZmqSocket(context, ZMQ_SUB);
  assert(exit_socket_ != NULL);

  err_t zisync_ret = exit_socket_->Connect(exit_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Transfer listen thread connect exit_uri(%s) fail: %s",
                exit_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  cmd_socket_router_ = new ZmqSocket(context, ZMQ_ROUTER);
  assert(cmd_socket_router_ != NULL);

  zisync_ret = cmd_socket_router_->Bind(cmd_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Transfer Startup bind cmd(%s) fail: %s",
                cmd_uri, zisync_strerror(zisync_ret));
    return zisync_ret;
  }


  // listen_thread
  listen_thread_ = new ListenThread("TransferServer", this);
  assert(listen_thread_ != NULL);

  if (listen_thread_->Startup() != 0) {
    ZSLOG_ERROR("Transfer server listen thread startup fail: %s",
                OsGetLastErr());
    return ZISYNC_ERROR_OS_THREAD;
  }

  return ZISYNC_SUCCESS;
}

err_t TransferServer::CleanUp() {
  if (listen_thread_ != NULL) {
    listen_thread_->Shutdown();
    delete listen_thread_;
    listen_thread_ = NULL;
  }

  if (listen_socket_ != NULL) {
    delete listen_socket_;
    listen_socket_ = NULL;
  }

  if (exit_socket_ != NULL) {
    delete exit_socket_;
    exit_socket_ = NULL;
  }

  if (cmd_socket_router_ != NULL) {
    delete cmd_socket_router_;
    cmd_socket_router_ = NULL;
  }

  for (auto it = std::begin(worker_threads_);
       it != std::end(worker_threads_); it++) {
    (*it)->Abort();
  }
  worker_threads_.clear();

  CancelAllTask();
  if (ctx_ != NULL) {
    SSL_CTX_free(ctx_);
    ctx_ = NULL;
  }

  return ZISYNC_SUCCESS;
}

IGetTask* TransferServer::CreateGetTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  GetTaskFactory* factory = GetTaskFactory::s_first_factory_;
  for (; factory != NULL; factory = factory->GetNextFactory()) {
    if (factory->GetFormat() == task_format) {
      return factory->CreateTask(
          monitor, local_tree_id, remote_tree_uuid, uri, ctx_);
    }
  }
  return NULL;
}

IDownloadTask* TransferServer::CreateDownloadTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  DownloadTaskFactory* factory = DownloadTaskFactory::s_first_factory_;
  for (; factory != NULL; factory = factory->GetNextFactory()) {
    if (factory->GetFormat() == task_format) {
      return factory->CreateTask(
          monitor, local_tree_id, remote_tree_uuid, uri, ctx_);
    }
  }
  return NULL;
}

IPutTask* TransferServer::CreatePutTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  PutTaskFactory* factory = PutTaskFactory::s_first_factory_;
  for (; factory != NULL; factory = factory->GetNextFactory()) {
    if (factory->GetFormat() == task_format) {
      return factory->CreateTask(
          monitor, local_tree_id, remote_tree_uuid, uri, ctx_);
    }
  }
  return NULL;
}

ITaskHandler* TransferServer::CreateTaskHandler(
    const std::string& task_method,
    const std::string& task_format) {
  TaskHandlerFactory* factory = TaskHandlerFactory::s_first_factory_;
  for (; factory != NULL; factory = factory->GetNextFactory()) {
    if (factory->GetMethod() == task_method
        && factory->GetFormat() == task_format) {
      return factory->CreateTaskHandler();
    }
  }
  return NULL;
}


ITreeAgent* TransferServer::GetTreeAgent() {
  return tree_agent_;
}

err_t TransferServer::CancelTask(int task_id) {
  GetTasksManager()->SetTaskAbort(task_id);
  return ZISYNC_SUCCESS;
}

err_t TransferServer::CancelAllTask() {
  GetTasksManager()->CancelAllTask();
  return ZISYNC_SUCCESS;
}

err_t TransferServer::QueryTaskInfoResult(QueryTransferTaskInfoResult *result) {
  // @TODO: implementation
  return zs::ZISYNC_ERROR_GENERAL;
}

int TransferServer::ListenRun() {
  assert(listen_socket_ != NULL);
  assert(exit_socket_ != NULL);
  while (1) {
    zmq_pollitem_t items[] = {
      {NULL, listen_socket_->fd(), ZMQ_POLLIN, 0},
      {exit_socket_->socket(), -1, ZMQ_POLLIN, 0},
      {cmd_socket_router_->socket(), -1, ZMQ_POLLIN, 0},
    };

    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), -1);
    if (ret == -1) {
      ZSLOG_ERROR("transfer listen thread zmq_poll fail: %s", OsGetLastErr());
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      OsTcpSocket* accept_socket;

      if (listen_socket_->Accept(&accept_socket) == -1) {
        ZSLOG_ERROR("Transfer listen thread accept fail.");
        continue;
      }
      assert(accept_socket != NULL);

      WorkerThread* child_thread =
          new WorkerThread("DataThread", this, accept_socket, ctx_);
      assert(child_thread != NULL);

      if (child_thread->Startup() != 0) {
        delete child_thread;
        ZSLOG_ERROR("Transfer listen thread start worker fail.");
        continue;
      }

      worker_threads_.push_back(child_thread);
    }

    if (items[1].revents & ZMQ_POLLIN) {
      return 0;
    }

    if (items[2].revents & ZMQ_POLLIN) {
      ZmqIdentify identify;
      err_t eno = identify.RecvFrom(*cmd_socket_router_);
      assert(eno == ZISYNC_SUCCESS);
      ZmqMsg request;
      eno = request.RecvFrom(*cmd_socket_router_);
      assert(eno == ZISYNC_SUCCESS);
      int32_t new_port = *static_cast<const int32_t*>(request.data());

      err_t ret = HandleSetPort(new_port);
      ZmqMsg response;
      response.SetData(&ret, sizeof(ret));
      eno = identify.SendTo(*cmd_socket_router_);
      assert(eno == ZISYNC_SUCCESS);
      eno = response.SendTo(*cmd_socket_router_, 0);
      assert(eno == ZISYNC_SUCCESS);
    }
  }

  return ZISYNC_SUCCESS;
}

PutTaskFactory* PutTaskFactory::s_first_factory_ = NULL;
TarPutTaskFactory TarPutTaskFactory::s_tar_put_task_factory_;
GetTaskFactory* GetTaskFactory::s_first_factory_ = NULL;
TarGetTaskFactory TarGetTaskFactory::s_tar_get_task_factory_;
DownloadTaskFactory* DownloadTaskFactory::s_first_factory_ = NULL;
TarDownloadTaskFactory TarDownloadTaskFactory::s_tar_download_task_factory_;
TaskHandlerFactory* TaskHandlerFactory::s_first_factory_ = NULL;
TarGetHandlerFactory TarGetHandlerFactory::s_tar_get_handler_factory_;
TarPutHandlerFactory TarPutHandlerFactory::s_tar_put_handler_factory_;

TasksManager::TasksManager() : ids_(0), cancel_all_task_(false) {
  mutex_ = new OsMutex();
  int ret = mutex_->Initialize();
  assert(ret == 0);
}
TasksManager::~TasksManager() {
  int ret = mutex_->CleanUp();
  assert(ret == 0);
  delete mutex_;
  tasks_.clear();
}

}  // namespace zs
