// Copyright 2014, zisync.com

#include <memory>
#include <cstdio>
#include <vector> 
#include <set>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/worker/inner_worker.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/read_fs_task.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/worker/sync_file.h"
#include "zisync/kernel/utils/token.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/platform.h"
// #include "zisync/kernel/worker/report_monitor.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/utils/tree_traverse.h"
#include "zisync/kernel/utils/sync_updater.h"
#include "zisync/kernel/utils/reportdata_handler.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/monitor/fs_monitor.h"
#include "zisync/kernel/history/history_manager.h"
#include "zisync/kernel/monitor/monitor.h"

namespace zs {

using std::unique_ptr;
using std::vector;
using std::set;

class MonitorReportEventHanlder : public MessageHandler {
 public:
  virtual ~MonitorReportEventHanlder() {
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
  MsgMonitorReportEventRequest request_msg_;
};

class IssuePushDeviceInfoReq : public IssueRequest {
 public:
  IssuePushDeviceInfoReq(
      int32_t remote_device_id, const string &remote_device_uuid, 
      const char *remote_ip, int32_t remote_route_port, bool is_mine):
      IssueRequest(remote_device_id, remote_device_uuid, remote_ip, 
                   remote_route_port, is_mine) {}
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }
  void set_request(const PushDeviceInfoRequest &request_) {
    request.mutable_request()->CopyFrom(request_.request());
  }
  
  PushDeviceInfoRequest request;
  PushDeviceInfoResponse response;
 private:
};

class IssueAnnounceTokenChangedReq : public IssueRequest {
 public:
  IssueAnnounceTokenChangedReq(
      int32_t remote_device_id, const string &remote_device_uuid, 
      const char *remote_ip, int32_t remote_route_port, bool):
      IssueRequest(remote_device_id, remote_device_uuid, remote_ip, 
                   remote_route_port, false) {}
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }
  void set_request(const AnnounceTokenChangedRequest &request_) {
    request.mutable_request()->CopyFrom(request_.request());
  }
  
  AnnounceTokenChangedRequest request;
  AnnounceTokenChangedResponse response;
};

template<typename IssueRequest, typename Request>
void AddRequestForDevice(
    IssueRequests<IssueRequest> *reqs, Request *request,
    int32_t device_id, const string &device_uuid, 
    int32_t device_route_port, bool is_mine) {
  IContentResolver *resolver = GetContentResolver();
  const char *device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };
  unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
          TableDeviceIP::URI, device_ip_projs, 
          ARRAY_SIZE(device_ip_projs), "%s = %d",
          TableDeviceIP::COLUMN_DEVICE_ID, device_id));

  while (device_ip_cursor->MoveToNext()) {
    IssueRequest *issue_push_device_info_req = 
        new IssueRequest(
            device_id, device_uuid, device_ip_cursor->GetString(0), 
            device_route_port, is_mine);
    issue_push_device_info_req->set_request(*request);
    reqs->IssueOneRequest(issue_push_device_info_req);
  }
}

static inline void AddPushDeviceInfoReqForDevice(
    IssueRequests<IssuePushDeviceInfoReq> *reqs, 
    PushDeviceInfoRequest *request, int32_t device_id, 
    const string &device_uuid, int32_t device_route_port, bool is_mine) {
  SetBackupRootInMsgDevice(
      device_id, request->mutable_request()->mutable_device());
  AddRequestForDevice(reqs, request, device_id, device_uuid, 
                      device_route_port, is_mine);
}

class IssuePushDeviceInfoHandler : public MessageHandler {
 public:
  virtual ~IssuePushDeviceInfoHandler() {
    /* virtual desctrutor */
  }

  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssuePushDeviceInfoRequest request_msg_;
};

err_t IssuePushDeviceInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (!Config::is_push_device_info_enabled()) {
    return ZISYNC_ERROR_GENERAL;
  }
#endif
  ZSLOG_INFO("Start IssuePushDeviceInfo");
  
  unique_ptr<PushDeviceInfoRequest> request_for_my_device;
  unique_ptr<PushDeviceInfoRequest> request_for_others_device;
  
  IContentResolver *resolver = GetContentResolver();
  IssueRequests<IssuePushDeviceInfoReq> issue_push_device_info_reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  {
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_UUID, 
    };
    unique_ptr<ICursor2> device_cursor;
    if (request_msg_.has_device_id()) { 
      int32_t device_id = request_msg_.device_id();
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s = %d", TableDevice::COLUMN_ID, device_id,
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
    } else {
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s != %d", 
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
              TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));
    }
    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      int32_t route_port = device_cursor->GetInt32(1);
      bool is_mine = device_cursor->GetBool(2);
      const string device_uuid = device_cursor->GetString(3);

      PushDeviceInfoRequest request_;
      if (is_mine) {
        if (!request_for_my_device) {
          request_for_my_device.reset(new PushDeviceInfoRequest);
          // Set all share and normal synces and trees
          SetMsgDeviceForMyDevice(
              request_for_my_device->mutable_request()->mutable_device());
        }
        request_.mutable_msg()->CopyFrom(request_for_my_device->request());
        AddBackupInMsgDevice(
            request_.mutable_request()->mutable_device(), device_id);
      } else {
        if (!request_for_others_device) {
          request_for_others_device.reset(new PushDeviceInfoRequest);
          // only set device
          SetDeviceMetaInMsgDevice(
              request_for_others_device->mutable_request()->mutable_device());
        }
        request_.mutable_msg()->CopyFrom(request_for_others_device->request());
        AddShareSyncInMsgDevice(
            request_.mutable_request()->mutable_device(), device_id);
      }
      AddPushDeviceInfoReqForDevice(
          &issue_push_device_info_reqs, &request_, 
          device_id, device_uuid, route_port, is_mine);
    }
  }

  while(issue_push_device_info_reqs.RecvNextResponsedRequest() != NULL) {
  }
  issue_push_device_info_reqs.UpdateDeviceInDatabase();

  IssuePushDeviceInfoResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End IssuePushDeviceInfo");
  return ZISYNC_SUCCESS;
}


class IssuePushDeviceMetaHandler : public MessageHandler {
 public:
  virtual ~IssuePushDeviceMetaHandler() {
    /* virtual desctrutor */
  }

  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

 private:
  MsgIssuePushDeviceMetaRequest request_msg_;
};

err_t IssuePushDeviceMetaHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (!Config::is_push_device_info_enabled()) {
    return ZISYNC_ERROR_GENERAL;
  }
#endif
  PushDeviceInfoRequest request;
  SetDeviceMetaInMsgDevice(request.mutable_request()->mutable_device());

  IssueRequests<IssuePushDeviceInfoReq> reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  {
    IContentResolver *resolver = GetContentResolver();
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_UUID,
    };
    unique_ptr<ICursor2> device_cursor;
    if (request_msg_.has_device_id()) { 
      int32_t device_id = request_msg_.device_id();
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s = %d", TableDevice::COLUMN_ID, device_id,
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
    } else {
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s != %d", 
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
              TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));
    }
    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      int32_t route_port = device_cursor->GetInt32(1);
      bool is_mine = device_cursor->GetBool(2);
      const string device_uuid = device_cursor->GetString(3);
      AddPushDeviceInfoReqForDevice(
          &reqs, &request, device_id, device_uuid, route_port, is_mine);
    }
  }
  while(reqs.RecvNextResponsedRequest() != NULL) {
  }
  reqs.UpdateDeviceInDatabase();

  IssuePushDeviceMetaResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

class IssuePushSyncInfoHandler : public MessageHandler {
 public:
  virtual ~IssuePushSyncInfoHandler() {
    /* virtual desctrutor */
  }

  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssuePushSyncInfoRequest request_msg_;
};


err_t IssuePushSyncInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (!Config::is_push_device_info_enabled()) {
    return ZISYNC_ERROR_GENERAL;
  }
#endif
  ZSLOG_INFO("Start IssuePushSyncInfo");
  
  const int32_t sync_id = request_msg_.sync_id();
  unique_ptr<Sync> local_sync(Sync::GetBy(
          "%s = %d", TableSync::COLUMN_ID, sync_id));
  if (!local_sync) {
    ZSLOG_ERROR("Noent Sync(%d)", sync_id);
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  bool creator_is_mine = Device::IsMyDevice(local_sync->device_id());
  IssueRequests<IssuePushDeviceInfoReq> reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  {
    PushDeviceInfoRequest request;
    MsgDevice *device = request.mutable_request()->mutable_device();
    SetDeviceMetaInMsgDevice(device);
    unique_ptr<MsgSync> sync_not_backup;
    
    IContentResolver *resolver = GetContentResolver();
    unique_ptr<ICursor2> device_cursor;
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_UUID, 
    };
    if (request_msg_.has_device_id()) { 
      int32_t device_id = request_msg_.device_id();
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s = %d", TableDevice::COLUMN_ID, device_id,
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
    } else {
      device_cursor.reset(resolver->Query(
              TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
              "%s = %d AND %s != %d", 
              TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
              TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));
    }

    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      int32_t route_port = device_cursor->GetInt32(1);
      bool is_mine = device_cursor->GetBool(2);
      const string device_uuid = device_cursor->GetString(3);
      PushDeviceInfoRequest request_;
      request_.mutable_msg()->CopyFrom(request.request());
      if (is_mine) {
        MsgSync *sync_ = request_.mutable_request()
            ->mutable_device()->add_syncs();
        if (local_sync->type() == TableSync::TYPE_BACKUP) {
          local_sync->ToMsgSync(sync_);
          if (local_sync->device_id() != TableDevice::LOCAL_DEVICE_ID) {
            // only call when delete backup in dst
            assert(request_msg_.has_device_id());
            SetTreesInMsgSyncBackupDst(sync_id, sync_);
          } else {
            SetTreesAndSyncModeInMsgSyncBackupSrc(
                sync_id, device_id, sync_);
          }
        } else {
          if (!creator_is_mine || 
              local_sync->perm() == TableSync::PERM_DISCONNECT || 
              local_sync->perm() == TableSync::PERM_TOKEN_DIFF) {
            continue;
          }
          if (!sync_not_backup) {
            sync_not_backup.reset(new MsgSync);
            local_sync->ToMsgSync(sync_not_backup.get());
            SetTreesInMsgSync(local_sync->id(), sync_not_backup.get());
          }
          sync_->CopyFrom(*sync_not_backup);
        }
      } else { // not MINE
        if (local_sync->type() != TableSync::TYPE_SHARED) {
          continue;
        }
        int32_t sync_perm = -1;
        if (local_sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
          err_t zisync_ret = GetShareSyncPerm(device_id, sync_id, &sync_perm);
          if (zisync_ret != ZISYNC_SUCCESS) {
            ZSLOG_ERROR("GetShareSyncPerm fail : %s", 
                        zisync_strerror(zisync_ret));
            continue;
          }
        } else {
          if (local_sync->device_id() != device_id) {
            continue;
          }
        }
        MsgSync *sync_ = request_.mutable_request()->mutable_device()
            ->add_syncs();
        if (!sync_not_backup) {
          sync_not_backup.reset(new MsgSync);
          local_sync->ToMsgSync(sync_not_backup.get());
          SetTreesInMsgSync(local_sync->id(), sync_not_backup.get());
        }
        sync_->CopyFrom(*sync_not_backup);
        if (sync_perm != -1) {
          sync_->set_perm(SyncPermToMsgSyncPerm(sync_perm));
        }
      }
      AddPushDeviceInfoReqForDevice(
          &reqs, &request_, device_id, device_uuid, route_port, is_mine);
    }
  } 
  while(reqs.RecvNextResponsedRequest() != NULL) {
  }
  reqs.UpdateDeviceInDatabase();

  IssuePushSyncInfoResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End IssuePushSyncInfo");
  return ZISYNC_SUCCESS;
}

class IssuePushTreeInfoHandler : public MessageHandler {
 public:
  virtual ~IssuePushTreeInfoHandler() {
    /* virtual desctrutor */
  }

  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssuePushTreeInfoRequest request_msg_;
};

err_t IssuePushTreeInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (!Config::is_push_device_info_enabled()) {
    return ZISYNC_ERROR_GENERAL;
  }
#endif
  ZSLOG_INFO("Start IssuePushTreeInfo");
  IContentResolver *resolver = GetContentResolver();
  
  const int32_t tree_id = request_msg_.tree_id();
  PushDeviceInfoRequest request;
  MsgDevice * device = request.mutable_request()->mutable_device();

  SetDeviceMetaInMsgDevice(device);
  const char* tree_projs[] = {
    TableTree::COLUMN_ROOT, TableTree::COLUMN_UUID, 
    TableTree::COLUMN_SYNC_ID, TableTree::COLUMN_STATUS,
  };

  unique_ptr<Sync> local_sync;
  int32_t sync_id;
  {
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d", 
            TableTree::COLUMN_ID, tree_id, 
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));//todo: should forbid root_moved
    if (!tree_cursor->MoveToNext()) {
      return ZISYNC_ERROR_TREE_NOENT;
    }
    sync_id = tree_cursor->GetInt32(2);
    // only push to my device
    local_sync.reset(Sync::GetBy(
            "%s = %d AND %s != %d AND %s != %d", 
            TableSync::COLUMN_ID, sync_id,
            TableSync::COLUMN_PERM, TableSync::PERM_DISCONNECT,
            TableSync::COLUMN_PERM, TableSync::PERM_TOKEN_DIFF));
    if (!local_sync) {
      ZSLOG_ERROR("Noent Sync(%d)", sync_id);
      return ZISYNC_ERROR_SYNC_NOENT;
    }
    MsgSync *sync = device->add_syncs();
    local_sync->ToMsgSync(sync);
    MsgTree *tree = sync->add_trees();
    tree->set_root(tree_cursor->GetString(0));
    tree->set_uuid(tree_cursor->GetString(1));
    assert(tree_cursor->GetInt32(3) != TableTree::STATUS_VCLOCK);
    tree->set_is_normal(tree_cursor->GetInt32(3) == TableTree::STATUS_NORMAL);
  }
  assert(local_sync->type() != TableSync::TYPE_BACKUP);
  bool creator_is_mine = Device::IsMyDevice(local_sync->device_id());

  IssueRequests<IssuePushDeviceInfoReq> reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  {
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_UUID, 
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
            "%s = %d AND %s != %d", 
            TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE,
            TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID));
    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      int32_t route_port = device_cursor->GetInt32(1);
      bool is_mine = device_cursor->GetBool(2);
      const string device_uuid = device_cursor->GetString(3);
      if (is_mine) {
        if (local_sync->type() == TableSync::TYPE_SHARED &&
            !creator_is_mine) {
          continue;
        }
      } else {
        if (local_sync->device_id() == TableDevice::LOCAL_DEVICE_ID) {
          err_t zisync_ret = GetShareSyncPerm(device_id, sync_id);
          if (zisync_ret != ZISYNC_SUCCESS) {
            ZSLOG_ERROR("GetShareSyncPerm fail : %s", 
                        zisync_strerror(zisync_ret));
            continue;
          }
        } else if (local_sync->device_id() != device_id) {
          continue;
        }
      }
      AddPushDeviceInfoReqForDevice(
          &reqs, &request, device_id, device_uuid, route_port, is_mine);
    }
  }
  while(reqs.RecvNextResponsedRequest() != NULL) {
  }
  reqs.UpdateDeviceInDatabase();
  
  IssuePushTreeInfoResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End IssuePushTreeInfo");
  return ZISYNC_SUCCESS;
}

#ifdef _WIN32
class ReportUiMonitorHandler : public MessageHandler {
 public:
  virtual ~ReportUiMonitorHandler() {
    /* virtual desctructor */
  }
  virtual MsgCode GetMsgCode() const { return MC_REPORT_UI_MONITOR_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t ReportUiMonitorHandler:: HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
      // @TODO: need portable
  // if (GetUIEventMonitor()->SendData() == true) {
  //   ReportUiMonitorResponse response;
  //   err_t not_ret = response.SendTo(socket);
  //   assert(not_ret == ZISYNC_SUCCESS);
  //   return ZISYNC_SUCCESS;
  // } else {
  //   return ZISYNC_ERROR_MONITOR;
  // }
  return zs::ZISYNC_SUCCESS;

  ReportUiMonitorResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

}
#endif

class IssueDeviceInfoReq : public IssueRequest {
 public:
  IssueDeviceInfoReq(
      const char *remote_ip, int32_t remote_route_port, bool is_ipv6_):
      IssueRequest(-1, remote_ip, remote_route_port, true), 
      is_ipv6(is_ipv6_), is_mine(true) {
        request.mutable_request()->set_device_uuid(Config::device_uuid());
      }
  IssueDeviceInfoReq(
      const char *remote_ip, int32_t remote_route_port, bool is_ipv6_,
      const char *sync_uuid_):
      IssueRequest(-1, remote_ip, remote_route_port, false), 
      is_ipv6(is_ipv6_), is_mine(false) {
        request.mutable_request()->set_sync_uuid(sync_uuid_);
        request.mutable_request()->set_device_uuid(Config::device_uuid());
      }
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }

  DeviceInfoRequest request;
  DeviceInfoResponse response;
  bool is_ipv6;
  bool is_mine;
 private:
  IssueDeviceInfoReq(IssueDeviceInfoReq&);
  void operator=(IssueDeviceInfoReq&);
};

class IssueDeviceInfoHandler : public MessageHandler {
 public:
  virtual ~IssueDeviceInfoHandler() {
    /* virtual desctrutor */
  }

  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:

  void IssueSingleDeviceInfoRequest(
      const ZmqContext &context, IssueDeviceInfoReq *request);
  MsgIssueDeviceInfoRequest request_msg_;
};

err_t IssueDeviceInfoHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (!Config::is_device_info_enabled()) {
    return ZISYNC_ERROR_GENERAL;
  }
#endif

  ZSLOG_INFO("Start IssueDeviceInfo");
  IContentResolver *resolver = GetContentResolver();
  DeviceInfoResponse response;
  IssueRequests<IssueDeviceInfoReq> issue_device_info_reqs(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
  bool device_online = false; // device online, so issue a sync 

  {
    if (request_msg_.has_uri()) {
      // call when find device by broadcast or DHT, meas device just online
      IssueDeviceInfoReq *issue_device_info_req;
      if (request_msg_.has_sync_uuid()) {
        issue_device_info_req = new IssueDeviceInfoReq(
            request_msg_.uri().host().c_str(), request_msg_.uri().port(), 
            request_msg_.uri().is_ipv6(),
            request_msg_.sync_uuid().c_str());
      } else {
        issue_device_info_req = new IssueDeviceInfoReq(
            request_msg_.uri().host().c_str(), 
            request_msg_.uri().port(), request_msg_.uri().is_ipv6());
      }
      issue_device_info_reqs.IssueOneRequest(issue_device_info_req);
      device_online = true;
    } else {
      // get all device
      const char *peer_projs[] = {
        TableDHTPeer::COLUMN_PEER_HOST, TableDHTPeer::COLUMN_PEER_PORT,
        TableDHTPeer::COLUMN_PEER_IS_IPV6, 
      };
      string account_name_sha;
      Sha1Hex(Config::account_name(), &account_name_sha);
      unique_ptr<ICursor2> peer_cursor(resolver->Query(
              TableDHTPeer::URI, peer_projs, ARRAY_SIZE(peer_projs),
              "%s = '%s'", TableDHTPeer::COLUMN_INFO_HASH, 
              account_name_sha.c_str()));
      while (peer_cursor->MoveToNext()) {
        const char *host = peer_cursor->GetString(0);
        int32_t port = peer_cursor->GetInt32(1);
        bool is_ipv6 = peer_cursor->GetBool(2);
        IssueDeviceInfoReq *issue_device_info_req = new IssueDeviceInfoReq(
            host, port, is_ipv6);
        issue_device_info_reqs.IssueOneRequest(issue_device_info_req);
      }

      const char *sync_projs[] = {
        TableSync::COLUMN_UUID,
      };
      string sync_uuid_sha;
      unique_ptr<ICursor2> sync_cursor(resolver->Query(
              TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs), 
              "%s = %d AND %s = %d", 
              TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED,
              TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
      while (sync_cursor->MoveToNext()) {
        const char *sync_uuid = sync_cursor->GetString(0);
        Sha1Hex(sync_uuid, &sync_uuid_sha);
        const char *peer_projs[] = {
          TableDHTPeer::COLUMN_PEER_HOST,
          TableDHTPeer::COLUMN_PEER_PORT, 
          TableDHTPeer::COLUMN_PEER_IS_IPV6,
        };
        unique_ptr<ICursor2> peer_cursor(resolver->Query(
                TableDHTPeer::URI, peer_projs, ARRAY_SIZE(peer_projs),
                "%s = '%s'", TableDHTPeer::COLUMN_INFO_HASH, 
                sync_uuid_sha.c_str()));
        while (peer_cursor->MoveToNext()) {
          const char *host = peer_cursor->GetString(0);
          int32_t port = peer_cursor->GetInt32(1);
          bool is_ipv6 = peer_cursor->GetBool(2);
          IssueDeviceInfoReq *issue_device_info_req = 
              new IssueDeviceInfoReq(host, port, is_ipv6, sync_uuid);
          issue_device_info_reqs.IssueOneRequest(issue_device_info_req);
        }
      }
    }
  }

  while (true) {
    if (zs::IsAborted()) {
      return ZISYNC_SUCCESS;
    }

    IssueDeviceInfoReq *device_info_request = 
        issue_device_info_reqs.RecvNextResponsedRequest();
    if (device_info_request == NULL) {
      issue_device_info_reqs.UpdateDeviceInDatabase();
      break;
    }
    if (device_info_request->error() != ZISYNC_SUCCESS) {
      ZSLOG_INFO("Error encountered(%s), continue.", zisync_strerror(device_info_request->error()));
      continue;
    }
    const MsgDevice &device = device_info_request->response.response().device();
    // @TODO check src_uuid
    int32_t device_id = StoreDeviceIntoDatabase(
        device, device_info_request->remote_ip(), 
        device_info_request->is_mine, device_info_request->is_ipv6, false);
    // call IssueSyncWithDevice here to only Issue sync with exiting trees, 
    // becasue new insert tree will call sync in StoreTreeIntoDatabase,
    // which is called by StoreSyncIntoDatabase, called by
    // StoreDeviceIntoDatabase
    if (device_id == TableDevice::LOCAL_DEVICE_ID) {
      continue;
    } else if (device_id == -1) {
      ZSLOG_WARNING("StoreDeviceIntoDatabase(%s) fail", 
                    device_info_request->remote_ip());
      continue;
    }
    if (device_online) {
      IssuePushDeviceInfo(device_id);
      IssueSyncWithDevice(device_id);
      ZSLOG_INFO("iam online, i push.");
    }
  }
  issue_device_info_reqs.UpdateDeviceInDatabase();
  // QueryCache::GetInstance()->NotifySyncModify();
  // QueryCache::GetInstance()->NotifyBackupModify();

  IssueDeviceInfoResponse response_;
  err_t not_ret = response_.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZSLOG_INFO("End IssueDeviceInfo");
  return ZISYNC_SUCCESS;
}

err_t InnerWorker::Initialize() {
  err_t zisync_ret;
  assert(req != NULL);
  assert(exit != NULL);
  zisync_ret = req->Connect(router_inner_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit->Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class FsDirVisitor : public FsVisitor {
 public:
  FsDirVisitor(const string &root, int32_t tree_id):
      FsVisitor(root, tree_id){}
  bool AddDir(const MsgReportEvent &event);
  bool AddDir(const string &path, unique_ptr<FileStat> *file = NULL);
  void VisitDirs();

  virtual int Visit(const OsFileStat &stat);
 private:
  FsDirVisitor(FsDirVisitor&);
  void operator=(FsDirVisitor&);
  set<string> prepare_;
};

class FsEventTask : public ReadFsTask {
 public:
  FsEventTask(const string &tree_uuid_, int32_t tree_id_, int32_t type, 
              const MsgMonitorReportEventRequest &request_msg):
      ReadFsTask(tree_uuid_, request_msg.tree_root(), tree_id_, type), 
      tree_id(tree_id_), request_msg_(request_msg) {}
  //    monitor(tree_id, request_msg_.events_size()) {}

  virtual err_t Run();

 private:
  int32_t tree_id;
  MsgMonitorReportEventRequest request_msg_;
 // IndexMonitor monitor;
 
  class FsEventCmp {
    public:
      bool operator()(const FsEvent& lhs, const FsEvent& rhs) {
        return lhs.file_move_cookie < rhs.file_move_cookie;
      }
  };

  class FileMoveEventHandler {
    public:
      FileMoveEventHandler(FsEventTask *task, IndexMonitor *monitor):task_(task), monitor_(monitor){}

      bool AddEvent(const MsgReportEvent &event) {
        switch (event.type()) {
          case ET_MOVE_FROM:
            move_from_.emplace(FsEvent(FS_EVENT_TYPE_MOVE_FROM
                , event.path(), event.file_move_cookie()));
            return true;
          case ET_MOVE_TO:
            move_to_.emplace(FsEvent(FS_EVENT_TYPE_MOVE_TO
                , event.path(), event.file_move_cookie()));
            return true;
          default:
            return false;
        }
      }

      void HandleFileMoveEvents(OperationList *op_list) {
        assert(task_);
        assert(op_list);
        const Uri &uri = TableFile::GenUri(task_->tree_uuid_.c_str());
        auto from_iter = move_from_.begin();
        auto to_iter = move_to_.begin();
        FsEventCmp cmp;

        for(; from_iter != move_from_.end()
            && to_iter != move_to_.end();) {
          if (cmp(*from_iter, *to_iter)) {
            ++from_iter;
          }else if (cmp(*to_iter, *from_iter)) {
            ++to_iter;
          }else {
            const char *to = &(*(to_iter->path.begin())) + task_->tree_root_.length();
            const char *from = &(*(from_iter->path.begin())) + task_->tree_root_.length();
            if (task_->AddMoveFileOp(uri, from, to, monitor_)) {
              from_iter = move_from_.erase(from_iter);
              to_iter = move_to_.erase(to_iter);
            }else {
              ++from_iter;
              ++to_iter;
            }
          }
        }

      }

      void HandleUnpairedMoveEvents() {
        const Uri &uri = TableFile::GenUri(task_->tree_uuid_.c_str());
        for(auto from_iter = move_from_.begin(); from_iter != move_from_.end();) {
          const char *relative_path =
            &(*(from_iter->path.begin())) + task_->tree_root_.length();
          task_->HandleReportEvent(uri, relative_path);
          task_->AddRemoveDirOp(uri, relative_path);
          from_iter = move_from_.erase(from_iter);
        }

        for(auto to_iter = move_to_.begin(); to_iter != move_to_.end(); ++to_iter) {
          const char *relative_path =
            &(*(to_iter->path.begin())) + task_->tree_root_.length();
          task_->HandleReportEvent(uri, relative_path);

          FsVisitor visitor(task_->request_msg_.tree_root(), task_->tree_id);
          OsFsTraverser traverser(to_iter->path, &visitor);
          traverser.traverse();
          for (auto iter = visitor.files()->begin(); 
              iter != visitor.files()->end(); iter ++) {
            string tmp = (*iter)->path();
            monitor_->OnFileWillIndex((*iter)->path());
            task_->HandleReportEvent(uri, tmp.c_str(), unique_ptr<FileStat>(iter->release()));
            monitor_->OnFileIndexed(1);
          }
        }

        move_to_.clear();
      }

    private:
      FsEventTask *task_;
      IndexMonitor *monitor_;
      std::set<FsEvent, FsEventCmp> move_from_;
      std::set<FsEvent, FsEventCmp> move_to_;
  };

 private:
  unique_ptr<FileMoveEventHandler> file_move_handler_;
  void AddRemoveDirOp(const Uri &uri, const char *relative_path);
  err_t HandleReportEvent(
      const Uri &uri, const char* relative_path, unique_ptr<FileStat> file_stat);

  bool AddMoveFileOp(const Uri &uri, const char *from, const char *to, IndexMonitor *monitor);
  //convenient
  err_t HandleReportEvent(
      const Uri &uri, const char* relative_path);
};

bool FsDirVisitor ::AddDir (const MsgReportEvent &event) {
  return AddDir(event.path());
}

bool FsDirVisitor::AddDir(const string &path, unique_ptr<FileStat> *file_actual) {
  OsFileStat stat;
  int res = OsStat(path, string(), &stat);
  if (res == -1 || res == ENOENT) {
    return false;
  }
  if (file_actual) {
      file_actual->reset(new FileStat(stat, tree_root_));
  }
  if(stat.type == OS_FILE_TYPE_DIR) {
    string fixed = path;
    if(fixed.size() == 0) {
      return false;
    }
    if(fixed.at(fixed.size()-1) != '/') {
      fixed += '/';
    }
    prepare_.insert(fixed);
    return true;
  }else {
    return false;
  }
}

void FsDirVisitor ::VisitDirs() {
  for (auto iter = prepare_.begin(); 
       iter != prepare_.end(); ) {
    OsFsTraverser traverser(*iter, this);
    traverser.traverse();
    auto next = iter;
    ++next;
    const string &father = *iter;
    for(; next != prepare_.end(); ++next) {
      const string &child = *next;
      const string &prefix = child.substr(0, father.size());
      if( prefix != father ) break;
    }
    iter = next;
  }
}

int FsDirVisitor::Visit(const OsFileStat &stat) {
  if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id_)) {
    return -1;
  }
  if(stat.type == OS_FILE_TYPE_DIR) {
    ZSLOG_INFO("Visit dirctory(%s)", stat.path.c_str());
    files_.push_back(unique_ptr<FileStat>(new FileStat(stat, tree_root_)));
  }
  return 0;
}

err_t FsEventTask::Run() {
  const Uri &uri = TableFile::GenUri(tree_uuid_.c_str());
  FsVisitor visitor(request_msg_.tree_root(), tree_id);

  for (int i = 0; i < request_msg_.events_size(); i ++) {
    if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id)) {
      return ZISYNC_SUCCESS;
    }
    const MsgReportEvent &event = request_msg_.events(i);
    if (event.type() == zs::ET_MOVE_TO) { 
      OsFsTraverser traverser(event.path(), &visitor);
      traverser.traverse();
    }
  }

  IndexMonitor monitor(
      tree_id, request_msg_.events_size() + (int)visitor.files()->size());

  file_move_handler_.reset(new FileMoveEventHandler(this, &monitor));

  for (int i = 0; i < request_msg_.events_size(); i ++) {
    if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id)) {
      return ZISYNC_SUCCESS;
    }
    file_move_handler_->AddEvent(request_msg_.events(i));
  }
  file_move_handler_->HandleFileMoveEvents(&op_list);
  file_move_handler_->HandleUnpairedMoveEvents();

  for (int i = 0; i < request_msg_.events_size(); i ++) {
    if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id)) {
      return ZISYNC_SUCCESS;
    }

    const MsgReportEvent &event = request_msg_.events(i);

    if (event.type() == ET_MOVE_FROM || event.type() == ET_MOVE_TO) {
      continue;
    }

    const char *relative_path = 
        &(*(event.path().begin())) + tree_root_.length();
    if (event.type() == zs::ET_MOVE_FROM || event.type() == zs::ET_DELETE) {
      IContentResolver *resolver = GetContentResolver();
      const char* projs[] = {
        TableFile::COLUMN_TYPE, 
      };
      unique_ptr<ICursor2> cursor(resolver->Query(
              uri, projs, ARRAY_SIZE(projs),
              "%s = '%s'", TableFile::COLUMN_PATH, 
              GenFixedStringForDatabase(relative_path).c_str()));
      if (cursor->MoveToNext() && 
          cursor->GetInt32(0) == zs::OS_FILE_TYPE_DIR) {
        AddRemoveDirOp(uri, relative_path);
      }
    }
    
    err_t add_op_ret = ZISYNC_SUCCESS;
    monitor.OnFileWillIndex(relative_path);
    OsFileStat os_file_stat;
    int stat_ret = OsStat(
        event.path(), event.has_alias() ? event.alias() : string(), 
        &os_file_stat);
    if (stat_ret == -1) {
      ZSLOG_ERROR("OsStat(%s) fail : %s", event.path().c_str(), 
                  OsGetLastErr());
      continue;
    } else if (stat_ret == ENOENT) {
      add_op_ret = HandleReportEvent(uri, relative_path, unique_ptr<FileStat>());
    } else {
      add_op_ret = HandleReportEvent(uri, relative_path,//relative_path made from event,originally retrieved from stat
              unique_ptr<FileStat>(new FileStat(os_file_stat, tree_root_)));
    }
    
    if (add_op_ret == ZISYNC_SUCCESS) {
      monitor.OnFileIndexed(1);
    }else{
      if (add_op_ret == ZISYNC_ERROR_SHA1_FAIL) {
        Monitor::GetMonitor()->ReportFailBack(tree_root_, event);
      }
    }
  }

  ApplyBatchTail();

  return error;
}

err_t FsEventTask::HandleReportEvent(
    const Uri &uri, const char *relative_path, unique_ptr<FileStat> file_stat) {
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> cursor(resolver->Query(
          uri, FileStat::file_projs_without_remote_vclock, 
          FileStat::file_projs_without_remote_vclock_len, 
          "%s = '%s'", TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(relative_path).c_str()));
  if (!file_stat) { // noent in fs
    if (cursor->MoveToNext()) {
      return AddRemoveFileOp(unique_ptr<FileStat>(new FileStat(cursor.get())));
    }
  } else {
    file_stat->time_stamp = OsTimeInS();
    if (cursor->MoveToNext()) {
;
      return AddUpdateFileOp(std::move(file_stat), unique_ptr<FileStat>(new FileStat(cursor.get())));
    } else {
      return AddInsertFileOp(std::move(file_stat));
    }
  }
  
  return ZISYNC_SUCCESS;
}

err_t FsEventTask::HandleReportEvent(
    const Uri &uri, const char *relative_path) {
    OsFileStat os_file_stat;
    int stat_ret = OsStat(tree_root_ + relative_path, string(), &os_file_stat);
    if (stat_ret == -1 || stat_ret == ENOENT) {
      return HandleReportEvent(uri, relative_path, unique_ptr<FileStat>());
    } else {
      return HandleReportEvent(uri, relative_path, unique_ptr<FileStat>(new FileStat(os_file_stat, tree_root_)));
    }
}

void FsEventTask::AddRemoveDirOp(const Uri &uri, const char *relative_path) {
  IContentResolver *resolver = GetContentResolver();
  has_change = true;
  unique_ptr<ICursor2> cursor(resolver->Query(
          uri, FileStat::file_projs_without_remote_vclock, 
          FileStat::file_projs_without_remote_vclock_len, 
          "%s LIKE '%s' || '/%%'  AND %s = %d", 
          TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(relative_path).c_str(), 
          TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL));
  while (cursor->MoveToNext()) {
    AddRemoveFileOp(unique_ptr<FileStat>(new FileStat(cursor.get())));
  }
}

bool FsEventTask::AddMoveFileOp(const Uri &uri, const char *from, const char *to, IndexMonitor *monitor) {

  IContentResolver *resolver = GetContentResolver();
  string fix_db_path = GenFixedStringForDatabase(from);

  Selection selection(
          "%s = '%s' OR %s LIKE '%s' || '/%%'",
          TableFile::COLUMN_PATH, fix_db_path.c_str(),
          TableFile::COLUMN_PATH, fix_db_path.c_str());

  unique_ptr<ICursor2> cursor(resolver->sQuery(
          uri, FileStat::file_projs_without_remote_vclock, 
          FileStat::file_projs_without_remote_vclock_len, 
          &selection, TableFile::COLUMN_PATH));

  FsVisitor visitor(tree_root_, tree_id);
  OsFsTraverser traverser(tree_root_ + to, &visitor);

  OsFileStat os_file_stat;
  int stat_ret = OsStat(tree_root_ + to, string(), &os_file_stat);
  if (stat_ret == -1 || stat_ret == ENOENT) {
    ZSLOG_ERROR("OsStat(%s) fail : %s", (tree_root_ + to).c_str(), 
        OsGetLastErr());
    return false;
  } else {
    visitor.files()->push_back(
        unique_ptr<FileStat>(new FileStat(os_file_stat, tree_root_)));
	assert(visitor.files()->size() > 0);
  }
  traverser.traverse();
  visitor.sort();
  assert(visitor.files()->size() > 0);

  size_t from_len = strlen(from);
  size_t to_len = strlen(to);
  const char *check_path_fs = NULL;
  const char *check_path_db = NULL;

  bool fs_db_all_match = true;
  bool db_empty = true;
  auto iter = visitor.files()->begin();
  
  while(cursor->MoveToNext()) {
    db_empty = false;
    unique_ptr<FileStat> afile_db(new FileStat(cursor.get()));
    assert(strncmp(afile_db->path(), from, from_len) == 0);

	if (iter == visitor.files()->end()) {
	  return false;
  }
    assert(strncmp((*iter)->path(), to, to_len) == 0);

    check_path_db = afile_db->path() + from_len;
    check_path_fs = (*iter)->path() + to_len;

    int cmp_res = strcmp(check_path_db, check_path_fs);
    assert(cmp_res >= 0);
    if (cmp_res > 0 || afile_db->status == zs::TableFile::STATUS_REMOVE) {
      ++iter;
      fs_db_all_match = false;
      continue;
    }

	string to_file = (*iter)->path();
    err_t add_op_ret = ZISYNC_SUCCESS;
    (*iter)->sha1.assign(afile_db->sha1);
	add_op_ret = HandleReportEvent(uri, to_file.c_str(), unique_ptr<FileStat>(iter->release()));
    assert(add_op_ret != ZISYNC_ERROR_SHA1_FAIL);//should not calc sha1

    OsFileStat os_file_stat_from;
	stat_ret = OsStat(tree_root_ + afile_db->path(), string(), &os_file_stat_from);
    if (stat_ret == -1 || stat_ret == ENOENT) {
      add_op_ret = HandleReportEvent(uri, afile_db->path(), unique_ptr<FileStat>());
    } else {
		add_op_ret = HandleReportEvent(uri, afile_db->path(), unique_ptr<FileStat>(new FileStat(os_file_stat_from, tree_root_)));
    }
    
    if (add_op_ret == ZISYNC_ERROR_SHA1_FAIL) {
      FsEvent evt;
      evt.path = tree_root_ + to_file;
      evt.type = FS_EVENT_TYPE_MODIFY;
      evt.file_move_cookie = 0;
      Monitor::GetMonitor()->ReportFailBack(tree_root_, evt);
    }
    ++iter;
  }

  return fs_db_all_match && !db_empty;
}

err_t MonitorReportEventHanlder::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  const string &tree_root = request_msg_.tree_root();
  unique_ptr<Tree> tree(Tree::GetBy(
          "%s = '%s' AND %s = %d AND %s = %d", 
          TableTree::COLUMN_ROOT, GenFixedStringForDatabase(tree_root).c_str(),
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));

  if (!tree) {
    ZSLOG_ERROR("Noent Tree(%s", tree_root.c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }

  int32_t type = ReadFsTask::GetType(*tree);
  if (type == -1) {
    ZSLOG_ERROR("ReadFsTask::GetType() fail");
    return ZISYNC_ERROR_GENERAL;
  }
  FsEventTask task(tree->uuid(), tree->id(), type, request_msg_);
  task.set_backup_type(tree->type());

  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(tree->sync_id()));
  ZSLOG_INFO("Start FsEventTask(%s)", tree_root.c_str());
  zs::GetEventNotifier()->NotifyIndexStart(tree->sync_id(), ToExternalSyncType(sync->type()));
  zisync_ret = task.Run();
  zs::GetEventNotifier()->NotifyIndexFinish(tree->sync_id(), ToExternalSyncType(sync->type()));
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  ZSLOG_INFO("End FsEventTask(%s)", tree_root.c_str());

  MonitorReportEventResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return zisync_ret;
}

class UpdateAllTreePairStatusHandler : public MessageHandler {
 public:
  virtual ~UpdateAllTreePairStatusHandler() {
    /* virtual desctructor */
  }
  virtual MsgCode GetMsgCode() const { return MC_REPORT_UI_MONITOR_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return NULL; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
};

err_t UpdateAllTreePairStatusHandler:: HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  class UpdateTreePairStatusTraverseVisitor : public ITreeTraverseVisitor {
   public:
    virtual ~UpdateTreePairStatusTraverseVisitor() {}
    virtual void TreePairVisit(
        int32_t local_tree_id, int32_t remote_tree_id) {
      SyncUpdater sync_updater(local_tree_id, remote_tree_id);
      if (!sync_updater.Initialize()) {
        ZSLOG_ERROR("SyncUpdater::Initialize fail");
        return;
      }

      sync_updater.Update();
    }
  };
  UpdateTreePairStatusTraverseVisitor visitor;
  TreeTraverse(&visitor);

  UpdateAllTreePairStatusResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  
  return ZISYNC_SUCCESS;
}

class IssueTokenChangedHandler : public MessageHandler {
 public:
  virtual ~IssueTokenChangedHandler() {
    /* virtual desctructor */
  }
  virtual MsgCode GetMsgCode() const { return MC_ISSUE_TOKEN_CHANGED_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { 
    return &request_msg_; 
  }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
 private:
  MsgIssueTokenChangedRequest request_msg_;
};

err_t IssueTokenChangedHandler:: HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  ZSLOG_INFO("Start IssueTokenChanged");
  IssueRequests<IssueAnnounceTokenChangedReq> reqs(1000);
  string sync_where;
  StringFormat(
      &sync_where, "%s != %d AND %s != %d",
      TableSync::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
      TableSync::COLUMN_DEVICE_ID, TableDevice::NULL_DEVICE_ID);
  string device_where;
  StringFormat(
      &device_where, "%s != %d AND %s != %d",
      TableDevice::COLUMN_ID, TableDevice::LOCAL_DEVICE_ID,
      TableDevice::COLUMN_ID, TableDevice::NULL_DEVICE_ID);
#ifdef ZS_TEST
  if (Config::is_announce_token_changed_enabled()) {
#else
  {
#endif
    AnnounceTokenChangedRequest request;
    request.mutable_request()->set_device_uuid(Config::device_uuid());
    request.mutable_request()->set_new_token(Config::token_sha1());
    IContentResolver *resolver = GetContentResolver();
    const char *device_projs[] = {
      TableDevice::COLUMN_ID, TableDevice::COLUMN_ROUTE_PORT,
      TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_UUID, 
    };
    unique_ptr<ICursor2> device_cursor(resolver->Query(
            TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
            "%s", device_where.c_str())); 
    while (device_cursor->MoveToNext()) {
      int32_t device_id = device_cursor->GetInt32(0);
      int32_t route_port = device_cursor->GetInt32(1);
      bool is_mine = device_cursor->GetBool(2);
      const string device_uuid = device_cursor->GetString(3);
      AddRequestForDevice(
          &reqs, &request, device_id, device_uuid, route_port, is_mine);
    }
  }
  TokenChangeToDiff(sync_where, device_where);

  // Set the sync that creator is not self, and not SOURCE_SHARED
  // as disconnect
  // QueryCache::GetInstance()->NotifySyncModify();

  /* Peer Erase, so if we change token back, the device can be found */
  IssueEraseAllPeer(request_msg_.origin_token().c_str());
  IssueDiscover();
  
  while(reqs.RecvNextResponsedRequest() != NULL) {
  }
  reqs.UpdateDeviceInDatabase();

  IssueTokenChangedResponse response;
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);

  ZmqSocket req(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = req.Connect(zs::router_inner_pull_fronter_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connect to (%s) fail.", router_inner_pull_fronter_uri);
    return zisync_ret;
  }

  IssuePushDeviceInfoRequest push_request;
  zisync_ret = push_request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
  
  ZSLOG_INFO("End IssueTokenChanged");
  return ZISYNC_SUCCESS;
}

InnerWorker::InnerWorker() : Worker("InnerWorker") {
  const MsgCode& msg_code = MC_MONITOR_REPORT_EVENT_REQUEST;
  msg_handlers_[msg_code]                      = new MonitorReportEventHanlder;
  msg_handlers_[MC_ISSUE_DEVICE_INFO_REQUEST] = new IssueDeviceInfoHandler;
  msg_handlers_[MC_ISSUE_PUSH_DEVICE_INFO_REQUEST] = new IssuePushDeviceInfoHandler;
  msg_handlers_[MC_ISSUE_PUSH_DEVICE_META_REQUEST] = new IssuePushDeviceMetaHandler;
  msg_handlers_[MC_ISSUE_PUSH_SYNC_INFO_REQUEST] = new IssuePushSyncInfoHandler;
  msg_handlers_[MC_ISSUE_PUSH_TREE_INFO_REQUEST] = new IssuePushTreeInfoHandler;
  // msg_handlers_[MC_REPORT_DATA_REQUEST] = new ReportDataHandler; 
  msg_handlers_[MC_UPDATE_ALL_TREE_PAIR_STATUS_REQUEST] = 
      new UpdateAllTreePairStatusHandler;
  msg_handlers_[MC_ISSUE_TOKEN_CHANGED_REQUEST] = new IssueTokenChangedHandler;

#ifdef _WIN32
  msg_handlers_[MC_REPORT_UI_MONITOR_REQUEST] = new ReportUiMonitorHandler;
#endif
}

}  // namespace zs
