// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_ROUTER_H_
#define ZISYNC_KERNEL_ROUTER_H_

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <set>

#include "zisync_kernel.h" // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/configure.h"
// #include "zisync/kernel/database/database.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/worker/outer_worker.h"
#include "zisync/kernel/worker/inner_worker.h"
#include "zisync/kernel/worker/refresh_worker.h"
#include "zisync/kernel/worker/sync_worker.h"
#include "zisync/kernel/utils/request.h"


namespace zs {

using std::string;
using std::vector;
using std::map;
using std::deque;
using std::shared_ptr;
using std::make_shared;
using std::set;

class MessageHandler;

class Router : public OsThread {
  friend class SetRoutePortHandler;
  friend class RouteStartupHandler;
  friend class RouteShutdownHandler;
 public:
  explicit Router();
  ~Router();

  virtual int Run();

  err_t Startup();
  int Shutdown();

 private:
  class SyncStatus {
   public:
    SyncStatus():status(MASK_IDLE) {}
    ~SyncStatus() {}

    int status;
    map<int /* remote_tree_id */, bool /* is_manual */> pending_remote_sync_ids;
   private:
    SyncStatus(SyncStatus&);
    void operator=(SyncStatus&);
  };

  err_t Initialize();
  /*  change the outer fronter port, just rebind the socket, not modify
   *  Config */
  err_t SetOuterFronterPort(int32_t new_port);
  err_t OuterFronterStartup();
  err_t OuterFronterShutdown();

  Router(Router&);
  void operator=(Router&);
  void DistributeOuterRequest();
  void GatherOuterResponse();
  void DistributeInnerPushRequest();
  void DistributeInnerRequest();
  void GatherInnerResponse();
  void DistributeRefreshRequest();
  void GatherRefreshResponse();
  void DistributeSyncRequest();
  void GatherSyncResponse();
  void Proxy(const ZmqSocket &src, const ZmqSocket &dst);
  inline void SendRefreshRequestToBackend(
      ZmqIdentify *identify, RefreshRequest *request) {
    err_t zisync_ret = identify->SendTo(refresh_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    // Send dummy client identify since we do not
    // have valid client identify
    ZmqIdentify empty_identify;
    zisync_ret = empty_identify.SendTo(refresh_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = request->SendTo(refresh_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
  void SendSyncRequestToBackend(
      ZmqIdentify *identify, SyncRequest *request) {
    err_t zisync_ret = identify->SendTo(sync_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    ZmqIdentify empty_identify;
    zisync_ret = empty_identify.SendTo(sync_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = request->SendTo(sync_backend);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }

  static const int MASK_WORK, MASK_IDLE, MASK_PEND;

  const ZmqContext &context_;
  std::unique_ptr<ZmqSocket> outer_fronter;
  ZmqSocket inner_fronter, inner_pull_fronter, sync_fronter, 
            refresh_fronter,
            outer_backend, inner_backend, sync_backend, refresh_backend,
            exit_sub, cmd;
  // SyncWorker *sync_workers;
  vector<shared_ptr<InnerWorker>> inner_workers;
  vector<shared_ptr<ZmqIdentify>> idle_outer_workers, idle_inner_workers,
      idle_refresh_workers, idle_sync_workers;

  map<int, int> refresh_status;  // SYNC, IDLE and PEND
  map<int, shared_ptr<SyncStatus>> sync_status;
  vector<shared_ptr<RefreshRequest>> refresh_requests;
  vector<shared_ptr<SyncRequest>> sync_requests;
  map<MsgCode, MessageHandler*> msg_handlers_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_ROUTER_H_

