// Copyright 2014, zisync.com

#include <memory>

#include "zisync_kernel.h"  //  NOLINT

#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/libevent/discover.h"
#include "zisync/kernel/utils/sha.h"

namespace zs {

using std::unique_ptr;

void IssuePushDeviceMeta(int32_t device_id /*  = -1 */) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushDeviceMetaRequest request;
  if (device_id != -1) {
    request.mutable_request()->set_device_id(device_id);
  }
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssuePushSyncInfo(int32_t sync_id, int32_t device_id /* = -1 */) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushSyncInfoRequest request;
  request.mutable_request()->set_sync_id(sync_id);
  if (device_id != -1) {
    request.mutable_request()->set_device_id(device_id);
  }
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssuePushDeviceInfo(int32_t device_id) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushDeviceInfoRequest request;
  request.mutable_request()->set_device_id(device_id);
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssueTokenChanged(const string &origin_token) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssueTokenChangedRequest request;
  request.mutable_request()->set_origin_token(origin_token);
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssuePushTreeInfo(int32_t tree_id) {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_inner_pull_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  IssuePushTreeInfoRequest request;
  request.mutable_request()->set_tree_id(tree_id);
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssueDiscoverBroadcast() {
  IDiscoverServer::GetInstance()->IssueBroadcast();
}

static inline void IssueDiscoverAnnounce() {
  IDiscoverServer::GetInstance()->IssueAnnounce();
}

void IssueDiscover() {
  IssueDiscoverAnnounce();
  IssueDiscoverBroadcast();
}

void IssueDiscoverSetBackground() {
  IDiscoverServer::GetInstance()->SetBackground();
}

void IssueDiscoverSetForeground() {
  IDiscoverServer::GetInstance()->SetForeground();
}

void IssueSync(int32_t local_tree_id, int32_t remote_tree_id) {
#ifdef ZS_TEST
  if (!Config::is_auto_sync_enabled()) {
    return;
  }
#endif
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  SyncRequest request;
  request.mutable_request()->set_local_tree_id(local_tree_id);
  request.mutable_request()->set_remote_tree_id(remote_tree_id);
  request.mutable_request()->set_is_manual(false);
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssueSyncWithRemoteTree(int32_t sync_id, int32_t tree_id) {
#ifdef ZS_TEST
  if (!Config::is_auto_sync_enabled()) {
    return;
  }
#endif
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID, 
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID, 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, 
          TableTree::COLUMN_IS_ENABLED, true));
  SyncRequest request;
  while (tree_cursor->MoveToNext()) {
    int32_t local_tree_id = tree_cursor->GetInt32(0);
    assert(local_tree_id != tree_id);
    request.mutable_request()->set_local_tree_id(local_tree_id);
    request.mutable_request()->set_remote_tree_id(tree_id);
    request.mutable_request()->set_is_manual(false);
    zisync_ret = request.SendTo(push);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
}


void IssueSyncWithLocalTree(int32_t sync_id, int32_t tree_id) {
#ifdef ZS_TEST
  if (!Config::is_auto_sync_enabled()) {
    return;
  }
#endif
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(zs::router_sync_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID, 
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ID, tree_id,
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
          TableTree::COLUMN_IS_ENABLED, true));
  if (!tree_cursor->MoveToNext()) {
    return;
  }
  tree_cursor.reset(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s != %d AND %s = %d", 
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_ID, tree_id,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  SyncRequest request;
  while (tree_cursor->MoveToNext()) {
    int32_t remote_tree_id = tree_cursor->GetInt32(0);
    request.mutable_request()->set_local_tree_id(tree_id);
    request.mutable_request()->set_remote_tree_id(remote_tree_id);
    request.mutable_request()->set_is_manual(false);
    zisync_ret = request.SendTo(push);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
}

void IssueRefresh(int32_t tree_id) {
  ZmqSocket refresh_push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = refresh_push.Connect(router_refresh_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  RefreshRequest request;
  MsgRefreshRequest *request_ = request.mutable_request();
  request_->set_tree_id(tree_id);
  zisync_ret = request.SendTo(refresh_push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void IssueRefreshWithSyncId(int32_t sync_id) {
  IContentResolver *resolver = GetContentResolver();
  ZmqSocket refresh_push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = refresh_push.Connect(router_refresh_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  RefreshRequest request;
  MsgRefreshRequest *request_ = request.mutable_request();
  
  const char *tree_projs[] = { TableTree::COLUMN_ID };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d",
          TableTree::COLUMN_SYNC_ID, sync_id,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  while (tree_cursor->MoveToNext()) {
    int32_t tree_id = tree_cursor->GetInt32(0);
    request_->set_tree_id(tree_id);
    zisync_ret = request.SendTo(refresh_push);
    assert(zisync_ret == ZISYNC_SUCCESS);
  }
}

void IssueAllRefresh() {
  IContentResolver* resolver = GetContentResolver();
  const char* projections[] = {
    TableTree::COLUMN_ID, TableTree::COLUMN_DEVICE_ID,
  };
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(router_refresh_fronter_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, projections, ARRAY_SIZE(projections), 
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,//todo: should also include root_moved trees
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
  while (cursor->MoveToNext()) {
    int32_t device_id = cursor->GetInt32(1);
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      RefreshRequest request;
      MsgRefreshRequest *request_ = request.mutable_request();
      request_->set_tree_id(cursor->GetInt32(0));
      err_t zisync_ret = request.SendTo(push);
      assert(zisync_ret == ZISYNC_SUCCESS);
    }
  }
}

void IssueEraseAllPeer(const char* account_name) {
  MsgDiscoverPeerEraseRequest msg_request;
  msg_request.set_erase_all(true);
  Sha1Hex(account_name, msg_request.mutable_info_hash_hex());
  IDiscoverServer::GetInstance()->PeerErase(msg_request);
}

void IssueSyncWithDevice(int32_t device_id) {
  assert(device_id != TableDevice::LOCAL_DEVICE_ID);

  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_ID, TableTree::COLUMN_SYNC_ID
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = %d AND %s = %d", 
          TableTree::COLUMN_DEVICE_ID, device_id,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  while (tree_cursor->MoveToNext()) {
    IssueSyncWithRemoteTree(tree_cursor->GetInt32(1), tree_cursor->GetInt32(0));
  }
}

err_t IssueRouteStartup() {
  err_t zisync_ret;
  RouteStartupRequest request;

  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  zisync_ret = req.Connect(router_cmd_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);

  RouteStartupResponse response;
  zisync_ret = response.RecvFrom(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("RouteStartup fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  return ZISYNC_SUCCESS;
}

err_t IssueRouteShutdown() {
  err_t zisync_ret;
  RouteShutdownRequest request;

  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  zisync_ret = req.Connect(router_cmd_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);

  RouteShutdownResponse response;
  zisync_ret = response.RecvFrom(req);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("RouteShutdown fail : %s",
                zisync_strerror(zisync_ret));
    return zisync_ret;
  }
  return ZISYNC_SUCCESS;
}

void IssueErasePeer(int32_t device_id, int32_t route_port) {
  IContentResolver *resolver = GetContentResolver();
  MsgDiscoverPeerEraseRequest msg_request;
  msg_request.set_erase_all(false);
  Sha1Hex(Config::account_name(), msg_request.mutable_info_hash_hex());
  {
    const char *device_ip_projs[] = {
      TableDeviceIP::COLUMN_IP, TableDeviceIP::COLUMN_IS_IPV6,
    };
    unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
            TableDeviceIP::URI, device_ip_projs, ARRAY_SIZE(device_ip_projs),
            "%s = %d", TableDeviceIP::COLUMN_DEVICE_ID, device_id));
    while (device_ip_cursor->MoveToNext()) {
      bool is_ipv6 = device_ip_cursor->GetBool(1);
      const char *ip = device_ip_cursor->GetString(0);
      MsgUri *uri = msg_request.add_peers();
      uri->set_host(ip);
      uri->set_port(route_port);
      uri->set_is_ipv6(is_ipv6);
    }
  }
  IDiscoverServer::GetInstance()->PeerErase(msg_request);
}

}  // namespace zs
