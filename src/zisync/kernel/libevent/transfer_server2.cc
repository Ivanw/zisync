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

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <memory>
#include <iostream>
#include <algorithm>
#include <tuple>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include "zisync/kernel/transfer/transfer.pb.h"
#pragma warning(pop)
#else
#include "zisync/kernel/transfer/transfer.pb.h"
#endif

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/libevent/tar_writer.h"
#include "zisync/kernel/libevent/transfer_connection.h"
#include "zisync/kernel/libevent/transfer_task.h"
#include "zisync/kernel/libevent/transfer_server2.h"
#include "zisync/kernel/libevent/tar_get_task.h"
#include "zisync/kernel/libevent/tar_put_task.h"
#include "zisync/kernel/libevent/tar_download_task.h"
#include "zisync/kernel/libevent/tar_upload_task.h"

namespace zs {

using std::unique_ptr;

TransferServer2 TransferServer2::s_instance;

ITransferServer* GetTransferServer2() {
  return &TransferServer2::s_instance;
}

/* class TransferServer */
TransferServer2::TransferServer2()
    : port_(0)
    , tree_agent_(NULL)
    , evbase_(NULL)
    , listener_(NULL)
    , next_task_id_(0) {
}

TransferServer2::~TransferServer2() {
  assert(listener_ == NULL);
}

err_t TransferServer2::Startup(ILibEventBase* base) {
  evbase_ = base;
  assert(port_ >= 0 && port_ <= 65535);
  return ChangeListenPort(port_);
}

err_t TransferServer2::Shutdown(ILibEventBase* base) {
  // abort all connection
  for (auto it = conn_map_.begin(); it != conn_map_.end(); ++it) {
    auto conn = it->second;
    conn->OnComplete(conn->bev_.get(), ZISYNC_ERROR_CANCEL);
  }
  conn_map_.clear();

  // abort all task
  for (auto it = task_map_.begin(); it != task_map_.end(); ++it) {
    auto task = it->second;
    task->OnComplete(task->bev_.get(), ZISYNC_ERROR_CANCEL);
  }
  task_map_.clear();
  
  // free listener.
  if (listener_) {
    evconnlistener_free(listener_);
    listener_ = NULL;
  }
  
  return ZISYNC_SUCCESS;
}

void TransferServer2::OnAccept(struct evconnlistener *listener,
                               evutil_socket_t socket,
                               struct sockaddr *sockaddr, int socklen) {
  
  struct event_base *base = evbase_->event_base();
  struct bufferevent *bev;

  int bsize = 256 * 1024;
  int rc = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (const char*)&bsize, sizeof(bsize));
  if (rc == -1) {
    ZSLOG_ERROR("Set socket recv buf length(256K) fail: %s", OsGetLastErr());
  }
  rc = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (const char*)&bsize, sizeof(bsize));
  if (rc == -1) {
    ZSLOG_ERROR("Set socket send buf length(256K) fail: %s", OsGetLastErr());
  }

  bev = bufferevent_socket_new(base, socket, BEV_OPT_CLOSE_ON_FREE);
  if (!bev) {
    ZSLOG_ERROR("Error constructing bufferevent!");
    // event_base_loopbreak(base);
    return;
  }

  int task_id = next_task_id_.FetchAndInc(1);
  auto connection = new TransferConnection(bev, task_id, this);

  // set read_cb, wirte_cb, event_cb for bev
  // bufferevent_setcb(
  //    bev, 
  //    [](struct bufferevent *bev, void *ctx) {
  //      reinterpret_cast<TransferConnection*>(ctx)->OnRead(bev);
  //    },
  //    [](struct bufferevent *bev, void *ctx) {
  //      reinterpret_cast<TransferConnection*>(ctx)->OnWrite(bev);
  //    },
  //    [](struct bufferevent *bev, short what, void *ctx) {
  //      reinterpret_cast<TransferConnection*>(ctx)->OnEvent(bev, what);
  //    },
  //    connection);
  bufferevent_setcb(
    bev, 
    LambdaOnRead<TransferConnection>,
    LambdaOnWrite<TransferConnection>,
    LambdaOnEvent<TransferConnection>,
    connection);
  
  bufferevent_setwatermark(bev, EV_READ, 0, 128 * 1024);
  // bufferevent_setwatermark(bev, EV_WRITE, 32 * 1024, 0);
  
  bufferevent_enable(bev, EV_READ);

  conn_map_[task_id] = std::shared_ptr<TransferConnection>(connection);
}

static err_t LambdaSetPort(void* ctx) {
  int32_t port;
  TransferServer2* server;
  std::tie(port, server) =
    *reinterpret_cast<std::tuple<int32_t, TransferServer2*>*>(ctx);
  return server->ChangeListenPort(port);
}
err_t TransferServer2::SetPort(int32_t new_port) {
  if (new_port <= 0 || new_port >= 65536) {
    return ZISYNC_ERROR_INVALID_PORT;
  }

  auto ctx = std::make_tuple(new_port, this);
  return evbase_->DispatchSync(LambdaSetPort, &ctx);
}

void TransferServer2::LambdaSchduleTask(void* ctx) {
  TransferServer2* server;
  TransferTask* task;
  auto ptr = reinterpret_cast<std::tuple<TransferTask*, TransferServer2*>*>(ctx); 
  std::tie(task, server) = *ptr;
  
  struct event_base *base = server->evbase_->event_base();
  struct bufferevent *bev;

  bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  if (!bev) {
    ZSLOG_ERROR("Error constructing bufferevent!");
    delete ptr;
    return;
  }

  task->bev_.reset(bev);

  bufferevent_setcb(
    bev, 
    LambdaOnRead<TransferTask>,
    LambdaOnWrite<TransferTask>,
    LambdaOnEvent<TransferTask>,
    task);
  
  bufferevent_setwatermark(bev, EV_READ, 0, 128 * 1024);
  bufferevent_setwatermark(bev, EV_WRITE, 32 * 1024, 0);
  
  bufferevent_enable(bev, EV_READ|EV_WRITE);

  server->task_map_[task->task_id_] = task;

  // issue connection
  UrlParser url(task->uri_);
  bufferevent_socket_connect_hostname(
      bev, server->evbase_->evdns_base(), AF_INET, url.host().data(), url.port());

  delete ptr;
}


err_t TransferServer2::ScheduleTask(TransferTask* task) {
  auto ctx = new std::tuple<TransferTask*, TransferServer2*>(task, this);
  evbase_->DispatchAsync(LambdaSchduleTask, ctx, NULL);
  return ZISYNC_SUCCESS;
}


err_t TransferServer2::Initialize(
   int32_t port, ITreeAgent* tree_agent, ISslAgent*) {
   port_ = port;
   tree_agent_ = tree_agent;

  return ZISYNC_SUCCESS;
}

err_t TransferServer2::CleanUp() {
  return ZISYNC_SUCCESS;
}

IGetTask* TransferServer2::CreateGetTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  auto task = new TarGetTask2(
      monitor, this, local_tree_id, remote_tree_uuid, uri);
  task->task_id_ = next_task_id_.FetchAndInc(1); 
  task->transfer_server_ = this;
  return task;
}

IDownloadTask* TransferServer2::CreateDownloadTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  auto task = new zs::DownloadTask2(
      monitor, this, local_tree_id, remote_tree_uuid, uri);
  task->task_id_ = next_task_id_.FetchAndInc(1); 
  task->transfer_server_ = this;
  return task;
}

IPutTask* TransferServer2::CreatePutTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  auto task =  new TarPutTask2(
      monitor, this, local_tree_id, remote_tree_uuid, uri);
  task->task_id_ = next_task_id_.FetchAndInc(1); 
  task->transfer_server_ = this;
  return task;
}

IUploadTask* TransferServer2::CreateUploadTask(
    ITaskMonitor* monitor,
    const int32_t local_tree_id,
    const char* task_format,
    const std::string& remote_tree_uuid,
    const std::string& uri) {
  auto task =  new UploadTask2(
      monitor, this, local_tree_id, remote_tree_uuid, uri);
  task->task_id_ = next_task_id_.FetchAndInc(1); 
  task->transfer_server_ = this;
  return task;
}

ITreeAgent* TransferServer2::GetTreeAgent() {
  return tree_agent_;
}

void TransferServer2::LambdaCancleTask(void* ctx) {
  int task_id;
  TransferServer2* server;
  std::tie(server, task_id) =
    *reinterpret_cast<std::tuple<TransferServer2*, int>*>(ctx);

  auto it = server->task_map_.find(task_id);
  if (it != server->task_map_.end()) {
    it->second->OnComplete(NULL, ZISYNC_ERROR_CANCEL);
    server->task_map_.erase(it);
  }

  delete reinterpret_cast<std::tuple<TransferServer2*, int>*>(ctx);
}

err_t TransferServer2::CancelTask(int task_id) {
  auto context = new std::tuple<TransferServer2*, int>(this, task_id);
  evbase_->DispatchAsync(LambdaCancleTask, context, NULL);
  return ZISYNC_SUCCESS;
}

void TransferServer2::LambdaCancleTaskByTree(void* ctx) {
  int tree_id;
  TransferServer2* server;
  std::tie(server, tree_id) =
    *reinterpret_cast<std::tuple<TransferServer2*, int>*>(ctx);
  ITreeAgent* tree_agent = server->GetTreeAgent();
  
  for (auto it = server->task_map_.begin();
       it != server->task_map_.end(); /* inc in loop */) {
    auto task = it->second;
    int32_t remote_tree_id =
        tree_agent->GetTreeId(task->remote_tree_uuid_);

    if (task->local_tree_id_ == tree_id || remote_tree_id == tree_id) {
      it = server->task_map_.erase(it);
      task->OnComplete(task->bev_.get(), ZISYNC_ERROR_CANCEL);
    } else {
      ++it;
    }
  }

  for (auto it = server->conn_map_.begin();
       it != server->conn_map_.end(); /* inc in loop */) {
    auto conn = it->second;
    if (conn->local_tree_id_ == tree_id || conn->remote_tree_id_ == tree_id) {
      conn->OnComplete(conn->bev_.get(), ZISYNC_ERROR_CANCEL);
      it = server->conn_map_.erase(it);
    } else {
      ++it;
    }
  }

  delete reinterpret_cast<std::tuple<TransferServer2*, int>*>(ctx);
}

err_t TransferServer2::CancelTaskByTree(int32_t tree_id) {
  auto ctx = new std::tuple<TransferServer2*, int>(this, tree_id);
  evbase_->DispatchAsync(LambdaCancleTaskByTree, ctx, NULL);
  return ZISYNC_SUCCESS;
}

err_t TransferServer2::QueryTaskInfoResult(QueryTransferTaskInfoResult *result) {
  return zs::ZISYNC_ERROR_GENERAL;
}

static void LambdaOnAccept(
    struct evconnlistener* listener, evutil_socket_t fd,
    struct sockaddr* sockaddr, int socklen, void* ctx) {
  ((TransferServer2*)ctx)->OnAccept(listener, fd, sockaddr, socklen);
}
  
  static void listener_error_cb(struct evconnlistener *listener, void *obj) {
    
  }

err_t TransferServer2::ChangeListenPort(int32_t new_port) {
  struct sockaddr_in sin;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(new_port);

  // struct evconnlistener* new_listener = evconnlistener_new_bind(
  //     evbase_->event_base(),
  //     [](struct evconnlistener *listener, evutil_socket_t fd,
  //        struct sockaddr * sockaddr,
  //        int socklen, void *ctx) {
  //       ((TransferServer2*)ctx)->OnAccept(listener, fd, sockaddr, socklen);
  //     }, this,
  //     LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
  //     -1, (struct sockaddr*)&sin, sizeof(sin));
  struct evconnlistener* new_listener = evconnlistener_new_bind(
      evbase_->event_base(), LambdaOnAccept, this,
      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
      -1, (struct sockaddr*)&sin, sizeof(sin));

  evconnlistener_set_error_cb(new_listener, listener_error_cb);
  if (!new_listener) {
    ZSLOG_ERROR("Could not create a listener!");
    return ZISYNC_ERROR_LIBEVENT;
  }

  if (listener_ != NULL) {
    evconnlistener_free(listener_);
  }
  port_ = new_port;
  listener_ = new_listener;
  
  return ZISYNC_SUCCESS;
}

void TransferServer2::RemoveConnect(int32_t task_id) {
  auto it = conn_map_.find(task_id);
  if (it != conn_map_.end()) {
    conn_map_.erase(it);
  }
}


void TransferServer2::RemoveTask(int32_t task_id) {
  auto it = task_map_.find(task_id);
  if (it != task_map_.end()) {
    task_map_.erase(it);
  }
}


}  // namespace zs
