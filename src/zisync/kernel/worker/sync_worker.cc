// Copyright 2014, zisync.com

#include <memory>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdio>

#include "zisync/kernel/worker/sync_worker.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/worker/sync_file_task.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/tree_mutex.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/sync_updater.h"

namespace zs {

using std::unique_ptr;
using std::shared_ptr;
using std::map;
using std::vector;

err_t SyncWorker::Initialize() {
  err_t zisync_ret;
  zisync_ret = req->Connect(router_sync_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit->Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class SyncTask {
 public:
  SyncTask(int32_t local_tree_id, int32_t remote_tree_id,
           bool is_manual_):
      sync_updater(local_tree_id, remote_tree_id),
      error(ZISYNC_SUCCESS), is_manual(is_manual_)
  {}

  void Run();

 private:
  void RunIntern();
  err_t UpdateRemoteMeta();
  inline bool IsAutoSync();
  
  SyncUpdater sync_updater;
  unique_ptr<Device> remote_device;

  err_t error;

  std::string remote_device_ip;
  
  bool is_manual;
};

void SyncTask::Run() {
  if (!sync_updater.Initialize()) {
    ZSLOG_ERROR("SyncUpdate::Initialize fail");
    return;
  }
  const Tree &local_tree = sync_updater.local_tree();
  const Tree &remote_tree = sync_updater.remote_tree();
  const Sync &sync = sync_updater.sync();
  
  remote_device.reset(Device::GetByIdWhereStatusOnline(
          remote_tree.device_id()));
  if (!remote_device) {
    ZSLOG_WARNING("Device(%d) does not exist or is offline", 
                  remote_tree.device_id());
    return;
  }  
  
  if (!local_tree.is_sync_enabled()) {
    return;
  }
  if (TreeMutex::TryLock(local_tree.id(), remote_tree.id())) {
    RunIntern();
    if (error == ZISYNC_SUCCESS) {
      UpdateLastSync(sync.id());
    }
    TreeMutex::Unlock(local_tree.id(), remote_tree.id());
  } else {
    ZSLOG_INFO("TryLock(%s, %s) fail", 
               local_tree.uuid().c_str(), remote_tree.uuid().c_str());
    error = ZISYNC_ERROR_GENERAL;
  }
}

bool SyncTask::IsAutoSync() {
  int sync_mode;
  const Tree &local_tree = sync_updater.local_tree();
  const Tree &remote_tree = sync_updater.remote_tree();
  if (local_tree.type() == TableTree::BACKUP_DST) {
    return false;
  }
  zs::GetSyncMode(
      local_tree.id(), local_tree.type(), remote_tree.id(), 
      &sync_mode, NULL);
  return sync_mode == SYNC_MODE_AUTO;
}

void SyncTask::RunIntern() {
  err_t zisync_ret = UpdateRemoteMeta(); 
  if (zisync_ret != ZISYNC_SUCCESS) {
    error = zisync_ret;
    ZSLOG_ERROR("Update fail : %s", zisync_strerror(zisync_ret));
    return ;
  }
  const Sync &sync = sync_updater.sync();
  sync_updater.Update(remote_device.get(), remote_device_ip.c_str());
  if (is_manual || IsAutoSync()) {
    zs::GetEventNotifier()->NotifySyncStart(sync.id(), ToExternalSyncType(sync.type()));
    SyncFileTask *sync_file_task = sync_updater.sync_file_task();
    sync_file_task->Run();
    if (!sync_file_task->IsAllSucces()) {
      error = ZISYNC_ERROR_GENERAL;
    }
    zs::GetEventNotifier()->NotifySyncFinish(sync.id(), ToExternalSyncType(sync.type()));
    err_t zisync_ret = UpdateRemoteMeta();
    if (zisync_ret != ZISYNC_SUCCESS) {
      error = zisync_ret;
      ZSLOG_ERROR("Update fail : %s", zisync_strerror(zisync_ret));
      return;
    }
    sync_updater.Update();
  }
}

err_t SyncTask::UpdateRemoteMeta() {
  const Tree &local_tree = sync_updater.local_tree();
  const Tree &remote_tree = sync_updater.remote_tree();
  const Sync &sync = sync_updater.sync();
  
  if (remote_device->id() == TableDevice::LOCAL_DEVICE_ID) {
    remote_device_ip = "127.0.0.1";
  } else {
    int64_t remote_since = 0;
    bool has_find = false;
    IContentResolver *resolver = GetContentResolver();
    err_t zisync_ret = GetTreeMaxUsnFromContent(
        remote_tree.uuid().c_str(), &remote_since);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("GetTreeMaxUsnFromContent fail : %s", 
                  zisync_strerror(zisync_ret));
      error = zisync_ret;
      return error;
    }

    MsgFindRequest request_base;
    request_base.set_local_tree_uuid(local_tree.uuid());
    request_base.set_remote_tree_uuid(remote_tree.uuid());
    request_base.set_sync_uuid(sync.uuid());
    request_base.set_limit(zs::FIND_LIMIT);
    request_base.set_is_list_sync(false);
    
    while (true) {
      IssueRequests<IssueFindRequest> issue_find_requests(
          WAIT_RESPONSE_TIMEOUT_IN_S * 1000);
      if (remote_device_ip.empty()) {
        const char *device_ip_projs[] = {
          TableDeviceIP::COLUMN_IP,
        };
        unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
                TableDeviceIP::URI, device_ip_projs, 
                ARRAY_SIZE(device_ip_projs), "%s = %d",
                TableDeviceIP::COLUMN_DEVICE_ID, remote_device->id()));
        while(device_ip_cursor->MoveToNext()) {
          IssueFindRequest *find_req = new IssueFindRequest(
              sync.id(), device_ip_cursor->GetString(0), 
              remote_device->route_port(), remote_tree.uuid().c_str());
          MsgFindRequest *msg_request = find_req->request.mutable_request();
          msg_request->CopyFrom(request_base);
          msg_request->set_since(remote_since);
          issue_find_requests.IssueOneRequest(find_req);
        }
      } else {
        IssueFindRequest *find_req = new IssueFindRequest(
            sync.id(), remote_device_ip.c_str(), remote_device->route_port(), 
            remote_tree.uuid().c_str());
        MsgFindRequest *msg_request = find_req->request.mutable_request();
          msg_request->CopyFrom(request_base);
        msg_request->set_since(remote_since);
        issue_find_requests.IssueOneRequest(find_req);
      }
      IssueFindRequest *find_request = 
          issue_find_requests.RecvNextResponsedRequest();
      if (find_request == NULL) {
        std::string ip_list;
        ZSLOG_ERROR("Recv Reponse timeout: %s", 
                    issue_find_requests.GetRemoteIpString(&ip_list));
        issue_find_requests.UpdateDeviceInDatabase();
        error = ZISYNC_ERROR_TIMEOUT;
        return error;
      } 
      if (find_request->error() != ZISYNC_SUCCESS) {
        error = find_request->error();
        ZSLOG_ERROR("Find fail : %s", zisync_strerror(find_request->error()));
        return error;
      }
      if (remote_device_ip.empty()) {
        remote_device_ip = find_request->remote_ip();
      }
      err_t zisync_ret = StoreRemoteMeta(
          sync.id(), remote_tree.uuid().c_str(), 
          find_request->response.response().remote_meta());
      if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("StoreFindResponse fail :  %s", zisync_strerror(zisync_ret));
        error = zisync_ret;
        return error;
      }
      has_find = true;
      const MsgRemoteMeta &remote_meta = 
          find_request->response.response().remote_meta();
      if (remote_meta.stats_size() == 0) {
        break;
      }
      remote_since = remote_meta.stats(remote_meta.stats_size() - 1).usn();
    }

    if (has_find) {
      ContentValues cv(1);
      cv.Put(TableTree::COLUMN_LAST_FIND, zs::OsTimeInS());
      resolver->Update(TableTree::URI, &cv, "%s = '%s'", 
                       TableTree::COLUMN_UUID, remote_tree.uuid().c_str());
    }
  }
  return ZISYNC_SUCCESS;
}

class SyncHanlder : public MessageHandler {
 public:
  virtual ~SyncHanlder() {
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
  void Handle();

 private:
  MsgSyncRequest request_msg_;
};

void SyncHanlder::Handle() {
  ZSLOG_INFO("Start Sync(%d, %d)", request_msg_.local_tree_id(),
             request_msg_.remote_tree_id());

  SyncTask sync_task(
      request_msg_.local_tree_id(), request_msg_.remote_tree_id(),
      request_msg_.is_manual());

  sync_task.Run();
  ZSLOG_INFO("End Sync(%d, %d)", request_msg_.local_tree_id(),
             request_msg_.remote_tree_id());
}

err_t SyncHanlder::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  Handle();

  SyncResponse response;
  response.mutable_response()->set_local_tree_id(
      request_msg_.local_tree_id());
  response.mutable_response()->set_remote_tree_id(
      request_msg_.remote_tree_id());
  err_t not_ret = response.SendTo(socket);
  assert(not_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

SyncWorker::SyncWorker():
      Worker("SyncWorker") {
        msg_handlers_[MC_SYNC_REQUEST] = new SyncHanlder();
      }

}  // namspace zs;
