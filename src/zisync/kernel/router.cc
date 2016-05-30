// Copyright 2014, zisync.com

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/router.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/configure.h"

namespace zs {

using std::unique_ptr;

const int Router::MASK_IDLE = 1;
const int Router::MASK_WORK = 2;
const int Router::MASK_PEND = 3;


class SetRoutePortHandler : public  MessageHandler {
 public:
  virtual ~SetRoutePortHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgSetRoutePortRequest request_msg_;
};

class RouteStartupHandler : public  MessageHandler {
 public:
  virtual ~RouteStartupHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

class RouteShutdownHandler : public  MessageHandler {
 public:
  virtual ~RouteShutdownHandler() {
    /* virtual desctrutor */
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

Router::Router()
    : OsThread("Router"), 
    context_(GetGlobalContext()),
    inner_fronter(context_, ZMQ_ROUTER),
    inner_pull_fronter(context_, ZMQ_PULL),
    sync_fronter(context_, ZMQ_PULL),
    refresh_fronter(context_, ZMQ_PULL),
    outer_backend(context_, ZMQ_ROUTER),
    inner_backend(context_, ZMQ_ROUTER),
    sync_backend(context_, ZMQ_ROUTER),
    refresh_backend(context_, ZMQ_ROUTER),
    exit_sub(context_, ZMQ_SUB),
      cmd(context_, ZMQ_ROUTER) {
  msg_handlers_[MC_ROUTE_STARTUP_REQUEST] = new RouteStartupHandler;
  msg_handlers_[MC_ROUTE_SHUTDOWN_REQUEST] = new RouteShutdownHandler;
  msg_handlers_[MC_SET_ROUTE_PORT_REQUEST] = new SetRoutePortHandler;
}

Router::~Router() {
  for (auto it = msg_handlers_.begin();
       it != msg_handlers_.end(); ++it) {
    delete it->second;
  }
  msg_handlers_.clear();
}

err_t Router::SetOuterFronterPort(int32_t new_port) {
  string uri;
  StringFormat(&uri, "tcp://*:%" PRId32, new_port);
  ZmqSocket new_socket(context_, ZMQ_ROUTER);

  err_t zisync_ret = new_socket.Bind(uri.c_str());
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Set New OuterFronter Port(%" PRId32") fail : %s", new_port,
        zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  outer_fronter->Swap(&new_socket);
  return ZISYNC_SUCCESS;
}

err_t Router::OuterFronterStartup() {
  if (!outer_fronter) {
    err_t zisync_ret = ZISYNC_SUCCESS;
    string uri;
    int32_t port = Config::DefaultRoutePort;
    outer_fronter.reset(new ZmqSocket(context_, ZMQ_ROUTER));
    do {
      StringFormat(&uri, "tcp://*:%" PRId32, port);
      zisync_ret = outer_fronter->Bind(uri.c_str());
      if (zisync_ret == ZISYNC_ERROR_ADDRINUSE) {
        port = OsGetRandomTcpPort();
        if (port == -1) {
          ZSLOG_ERROR("OsGetRandomTcpPort() fail : %s", OsGetLastErr());
          return ZISYNC_ERROR_OS_SOCKET;
        }
      } else if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("Bind outer_fronter(%s) fail : %s", uri.c_str(),
                    zisync_strerror(zisync_ret));
        return zisync_ret;
      }
    } while (zisync_ret == ZISYNC_ERROR_ADDRINUSE);
    if (port != Config::route_port()) {
      zisync_ret = SaveRoutePort(port);
      if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("SaveRoutePort() fail.");
        return zisync_ret;
      }
      Config::set_route_port(port);
    }
  }
  return ZISYNC_SUCCESS;
}

err_t Router::OuterFronterShutdown() {
  outer_fronter.reset(NULL);
  return ZISYNC_SUCCESS;
}

err_t Router::Initialize() {
  err_t zisync_ret = ZISYNC_SUCCESS;

  zisync_ret = OuterFronterStartup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("RouteStartup fail : %s", zisync_strerror(zisync_ret));
  }
  zisync_ret = outer_backend.Bind(router_outer_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = inner_fronter.Bind(router_inner_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = inner_pull_fronter.Bind(router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = inner_backend.Bind(router_inner_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = sync_fronter.Bind(router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = sync_backend.Bind(router_sync_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = refresh_fronter.Bind(router_refresh_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = refresh_backend.Bind(router_refresh_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = cmd.Bind(router_cmd_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit_sub.Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  // int ret = discover.Initialize();
  // if (ret != 0) {
  //   LOG(ERROR) << "Start discover fail: " << zisync_strerror(zisync_ret);
  //   return zisync_ret;
  // }
  // sync_workers = new SyncWorker[Config::sync_workers_num()];
  // if (sync_workers == NULL) {
  //   LOG(ERROR) << "new SyncWorker[" << Config::sync_workers_num() <<
  //       "] fail.";
  //   return ZISYNC_ERROR_MEMORY;
  // }
  // for (int i = 0; i < Config::sync_workers_num(); i ++) {
  //   zisync_ret = sync_workers[i].Startup();
  //   if (zisync_ret != ZISYNC_SUCCESS) {
  //     LOG(ERROR) << "sync_workers[" << i << "] fail : " <<
  //         zisync_strerror(zisync_ret);
  //     return zisync_ret;
  //   }
  // }

  //for (int i = 0; i < Config::inner_workers_num(); i ++) {
  //  inner_workers.push_back(make_shared<InnerWorker>(context_));
  //}

  return ZISYNC_SUCCESS;
}

err_t Router::Startup() {
  err_t zisync_ret = Router::Initialize();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  int ret = OsThread::Startup();
  if (ret == -1) {
    ZSLOG_ERROR("Start thread fail");
    return ZISYNC_ERROR_OS_THREAD;
  }
  
  return ZISYNC_SUCCESS;
}

int Router::Shutdown() {
  OsThread::Shutdown();
  return 0;
}

int Router::Run() {
  //for (int i = 0; i < Config::inner_workers_num(); i ++) {
  //  int ret = inner_workers[i]->Startup();
  //  assert(ret == 0);
  //}

  while (1) {
    // should re init for each loop
    void *outer_fronter_socket = outer_fronter ? 
        outer_fronter->socket() : NULL;

    zmq_pollitem_t items[] = {
      { sync_backend.socket(),    -1, ZMQ_POLLIN, 0 },  // 0
      { refresh_backend.socket(), -1, ZMQ_POLLIN, 0 },  // 1
      { outer_backend.socket(),   -1, ZMQ_POLLIN, 0 },  // 2
      { inner_backend.socket(),   -1, ZMQ_POLLIN, 0 },  // 3
      { sync_fronter.socket(),    -1, ZMQ_POLLIN, 0 },  // 4
      { refresh_fronter.socket(), -1, ZMQ_POLLIN, 0 },  // 5
      { outer_fronter_socket,     -1, ZMQ_POLLIN, 0 },  // 6
      { inner_fronter.socket(),   -1, ZMQ_POLLIN, 0 },  // 7 
      { exit_sub.socket(),        -1, ZMQ_POLLIN, 0 },  // 8
      { cmd.socket(),             -1, ZMQ_POLLIN, 0 },  // 9
      { inner_pull_fronter.socket(),   -1, ZMQ_POLLIN, 0 },
    };

    if (idle_outer_workers.empty()) {
      items[6].events = 0;
    }
    if (idle_inner_workers.empty()) {
      items[7].events = 0;    // inner_fronter
      items[10].events = 0;   // inner_pull_fonter
    }
    if (!outer_fronter) {
      items[6].events = 0;  // outer_fronter
    }

    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), -1);
    if (ret  == -1) {
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      GatherSyncResponse();
    }
    if (items[1].revents & ZMQ_POLLIN) {
      GatherRefreshResponse();
    }
    if (items[2].revents & ZMQ_POLLIN) {
      GatherOuterResponse();
    }
    if (items[3].revents & ZMQ_POLLIN) {
      GatherInnerResponse();
    }
    if (items[4].revents & ZMQ_POLLIN) {
      DistributeSyncRequest();
    }
    if (items[5].revents & ZMQ_POLLIN) {
      DistributeRefreshRequest();
    }
    if (items[6].revents & ZMQ_POLLIN) {
      DistributeOuterRequest();
    }
    if (items[8].revents & ZMQ_POLLIN) {
      break;
    }
    if (items[9].revents & ZMQ_POLLIN) {
      MessageContainer container(msg_handlers_, true);
      container.RecvAndHandleSingleMessage(cmd, this);
    }
    if (items[7].revents & ZMQ_POLLIN) {
      DistributeInnerRequest();
    } else if (items[10].revents & ZMQ_POLLIN) {
      DistributeInnerPushRequest();
    }
  }

  return 0;
}

inline void Router::Proxy(const ZmqSocket &src, const ZmqSocket &dst) {
  do {
    ZmqMsg msg;
    err_t zisync_ret = msg.RecvFrom(src);
    assert(zisync_ret == ZISYNC_SUCCESS);

    bool has_more = msg.HasMore();
    zisync_ret = msg.SendTo(dst, has_more ? ZMQ_SNDMORE : 0);
    assert(zisync_ret == ZISYNC_SUCCESS);

    if (!has_more) {
      break;
    }
  } while (1);
}

void Router::DistributeOuterRequest() {
  assert(!idle_outer_workers.empty());
  auto identify = idle_outer_workers.back();

  err_t zisync_ret = identify->SendTo(outer_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  idle_outer_workers.pop_back();
  assert(outer_fronter);
  Proxy(*outer_fronter, outer_backend);
}

void Router::GatherOuterResponse() {
  auto worker_identify = make_shared<ZmqIdentify>();
  err_t zisync_ret = worker_identify->RecvFrom(outer_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);
  idle_outer_workers.push_back(worker_identify);

  ZmqIdentify client_identify;
  zisync_ret = client_identify.RecvFrom(outer_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  if (!client_identify.IsReadyMsg()) {
    if (outer_fronter) {
      zisync_ret = client_identify.SendTo(*outer_fronter);
      assert(zisync_ret == ZISYNC_SUCCESS);
      Proxy(outer_backend, *outer_fronter);
    }
  }
}

void Router::DistributeInnerRequest() {
  assert(!idle_inner_workers.empty());
  auto identify = idle_inner_workers.back();

  err_t zisync_ret = identify->SendTo(inner_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  idle_inner_workers.pop_back();
  Proxy(inner_fronter, inner_backend);
}

void Router::DistributeInnerPushRequest() {
  assert(!idle_inner_workers.empty());
  auto identify = idle_inner_workers.back();

  err_t zisync_ret = identify->SendTo(inner_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  idle_inner_workers.pop_back();
  ZmqIdentify empty_indentify;
  /* fill an empty identify */
  zisync_ret = empty_indentify.SendTo(inner_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);
  Proxy(inner_pull_fronter, inner_backend);
}

void Router::GatherInnerResponse() {
  auto worker_identify = make_shared<ZmqIdentify>();
  err_t zisync_ret = worker_identify->RecvFrom(inner_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);
  idle_inner_workers.push_back(worker_identify);

  ZmqIdentify client_identify;
  zisync_ret = client_identify.RecvFrom(inner_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  if (!client_identify.IsReadyMsg()) {
    if (client_identify.IsEmptyIdentify()) {
      do {
        ZmqMsg msg;
        err_t zisync_ret = msg.RecvFrom(inner_backend);
        assert(zisync_ret == ZISYNC_SUCCESS);
        if (!msg.HasMore()) {
          break;
        }
      } while (1);
    } else {
      zisync_ret = client_identify.SendTo(inner_fronter);
      assert(zisync_ret == ZISYNC_SUCCESS);
      Proxy(inner_backend, inner_fronter);
    }
  }
}

void Router::DistributeRefreshRequest() {
  /* Refresh request is from inner
   * There is no identify */
  auto request = make_shared<RefreshRequest>();
  err_t zisync_ret;

  zisync_ret = request->RecvFrom(refresh_fronter);
  assert(zisync_ret == ZISYNC_SUCCESS);

  auto find = refresh_status.find(request->request().tree_id());
  if (find != refresh_status.end()) {
    if (find->second == MASK_WORK) {
      find->second = MASK_PEND;
      return;
    } else if (find->second == MASK_PEND) {  // PEND
      return;
    }  // IDLE, get a worker and send
  }

  // find a worker to distribute request
  if (!idle_refresh_workers.empty()) {
    auto identify = idle_refresh_workers.back();
    SendRefreshRequestToBackend(identify.get(), request.get());
    refresh_status[request->request().tree_id()] = MASK_WORK;
    idle_refresh_workers.pop_back();
  } else {
    refresh_status[request->request().tree_id()] = MASK_PEND;
    refresh_requests.push_back(request);
  }
}


void Router::GatherRefreshResponse() {
  auto worker_identify = make_shared<ZmqIdentify>();
  err_t zisync_ret = worker_identify->RecvFrom(refresh_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZmqIdentify client_identify;
  zisync_ret = client_identify.RecvFrom(refresh_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  if (!client_identify.IsReadyMsg()) {
    RefreshResponse response;
    zisync_ret = response.RecvFrom(refresh_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    auto find = refresh_status.find(response.response().tree_id());
    assert(find != refresh_status.end());
    assert(find->second != MASK_IDLE);
    if (find->second == MASK_WORK) {
      find->second = MASK_IDLE;
    } else if (find->second == MASK_PEND) {
      find->second = MASK_WORK;
      RefreshRequest request;
      request.mutable_request()->set_tree_id(response.response().tree_id());
      SendRefreshRequestToBackend(worker_identify.get(), &request);
      return;
    }
  }

  // not find a pending one, get one in the refresh_request
  if (refresh_requests.empty()) {
    idle_refresh_workers.push_back(worker_identify);
  } else {
    auto request = refresh_requests.back();
    SendRefreshRequestToBackend(worker_identify.get(), request.get());
    refresh_requests.pop_back();
    assert(refresh_status.find(request->request().tree_id())->second
           == MASK_PEND);
    refresh_status[request->request().tree_id()] = MASK_WORK;
  }
}

static inline void PendingRemoteTreeId(
    const MsgSyncRequest &request, map<int, bool> *remote_tree_ids) {
  if (remote_tree_ids->find(request.remote_tree_id()) == 
      remote_tree_ids->end()) {
    (*remote_tree_ids)[request.remote_tree_id()] = request.is_manual();
  } else {
    bool &is_manual = (*remote_tree_ids)[request.remote_tree_id()]; 
    is_manual = request.is_manual() | is_manual;
  }
}

void Router::DistributeSyncRequest() {
  auto request = make_shared<SyncRequest>();
  err_t zisync_ret;

  zisync_ret = request->RecvFrom(sync_fronter);
  assert(zisync_ret == ZISYNC_SUCCESS);

  auto find = sync_status.find(request->request().local_tree_id());
  if (find != sync_status.end()) {
    if (find->second->status == MASK_WORK ||
        find->second->status == MASK_PEND) {
      find->second->status = MASK_PEND;
      PendingRemoteTreeId(
          request->request(), &find->second->pending_remote_sync_ids);
      return;
    } else {
      assert(find->second->pending_remote_sync_ids.empty());
    }
  }

  // not find, or is MASK_IDLE
  // send the request to backend or cache it
  auto new_sync_status = make_shared<SyncStatus>();
  if (!idle_sync_workers.empty()) {
    auto identify = idle_sync_workers.back();
    SendSyncRequestToBackend(identify.get(), request.get());
    new_sync_status->status = MASK_WORK;
    idle_sync_workers.pop_back();
  } else {
    new_sync_status->status = MASK_PEND;
      PendingRemoteTreeId(
          request->request(), &new_sync_status->pending_remote_sync_ids);
    sync_requests.push_back(request);
  }
  sync_status[request->request().local_tree_id()] = new_sync_status;
}

void Router::GatherSyncResponse() {
  auto worker_identify = make_shared<ZmqIdentify>();
  err_t zisync_ret = worker_identify->RecvFrom(sync_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ZmqIdentify client_identify;
  zisync_ret = client_identify.RecvFrom(sync_backend);
  assert(zisync_ret == ZISYNC_SUCCESS);

  if (!client_identify.IsReadyMsg()) {
    SyncResponse response;
    zisync_ret = response.RecvFrom(sync_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    auto find = sync_status.find(response.response().local_tree_id());
    assert(find != sync_status.end());
    SyncStatus &find_sync_status = *(find->second);
    assert(find_sync_status.status != MASK_IDLE);
    if (find_sync_status.status == MASK_WORK) {
      assert(find_sync_status.pending_remote_sync_ids.empty());
      find_sync_status.status = MASK_IDLE;
    } else if (find_sync_status.status == MASK_PEND) {
      assert(!find_sync_status.pending_remote_sync_ids.empty());
      auto set_find = find_sync_status.pending_remote_sync_ids.begin();
      SyncRequest request;
      MsgSyncRequest *request_ = request.mutable_request();
      request_->set_local_tree_id(response.response().local_tree_id());
      request_->set_remote_tree_id(set_find->first);
      request_->set_is_manual(set_find->second);
      SendSyncRequestToBackend(worker_identify.get(), &request);
      find_sync_status.pending_remote_sync_ids.erase(set_find);
      if (find_sync_status.pending_remote_sync_ids.empty()) {
        find_sync_status.status = MASK_WORK;
      }
      return;
    }
  }

  if (sync_requests.empty()) {
    idle_sync_workers.push_back(worker_identify);
  } else {
    auto request = sync_requests.back();
    auto find = sync_status.find(request->request().local_tree_id());
    assert(find->second->status == MASK_PEND &&
           !find->second->pending_remote_sync_ids.empty());
    auto remote_tree_find = find->second->pending_remote_sync_ids.find(
        request->request().remote_tree_id());
    assert(remote_tree_find != find->second->pending_remote_sync_ids.end());

    request->mutable_request()->set_is_manual(remote_tree_find->second);
    SendSyncRequestToBackend(worker_identify.get(), request.get());
    sync_requests.pop_back();
    find->second->pending_remote_sync_ids.erase(
        request->request().remote_tree_id());
    if (find->second->pending_remote_sync_ids.empty()) {
      find->second->status = MASK_WORK;
    }
  }
}

err_t SetRoutePortHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  
  if (Config::route_port() == request_msg_.new_port()) {
    return ZISYNC_SUCCESS;
  }
  
  Router *router = reinterpret_cast<Router*>(userdata);
  zisync_ret = router->SetOuterFronterPort(request_msg_.new_port());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  zisync_ret = SaveRoutePort(request_msg_.new_port());
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  SetRoutePortResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  Config::set_route_port(request_msg_.new_port());
  
  return ZISYNC_SUCCESS;
}

err_t RouteShutdownHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Router *router = reinterpret_cast<Router*>(userdata);

  err_t zisync_ret = router->OuterFronterShutdown();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  RouteShutdownResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

err_t RouteStartupHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Router *router = reinterpret_cast<Router*>(userdata);

  err_t zisync_ret = router->OuterFronterStartup();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }

  RouteStartupResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

}  // namespace zs
