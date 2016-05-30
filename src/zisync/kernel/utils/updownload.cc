// Copyright 2015, zisync.com

#include <algorithm>
#include <functional>

#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/utils/updownload.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/sync_const.h"

namespace zs {

int UpDownloadThread::RunIntern() {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
    TableTree::COLUMN_ID, 
  };

  {
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %" PRId32 " AND %s = %d AND %s = %d", 
            TableTree::COLUMN_SYNC_ID, sync_id,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, //todo: if root_moved, should forbid download
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
    if (tree_cursor->MoveToNext()) {
      local_tree_uuid = tree_cursor->GetString(0);
      local_tree_id = tree_cursor->GetInt32(2);
    }
  }

  if (GetUpDownloadSource()) {
    assert(file_length != -1);
    assert(remote_tree_uuid.length() != 0);
    assert(remote_device_data_uri.length() != 0);

    err_t ret = ExecuteTransferTask();
    if (ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("ExecuteTransferTask fail : %s", zisync_strerror(ret));
      error = ret;
      return 0;
    }
  } else {
    ZSLOG_ERROR("Get Source fail");
    assert(error != ZISYNC_SUCCESS);
  }

  return 0;
}

static inline bool GetUpDownloadDeivce(
    int32_t device_id, string *device_uuid, int32_t *route_port, 
    int32_t *data_port) {
   if (device_id == TableDevice::LOCAL_DEVICE_ID) {
     return false;
   }
   assert(route_port != NULL);
   assert(data_port != NULL);
   IContentResolver *resolver = GetContentResolver();
   const char *device_projs[] = {
     TableDevice::COLUMN_TYPE, TableDevice::COLUMN_ROUTE_PORT,
     TableDevice::COLUMN_DATA_PORT, TableDevice::COLUMN_UUID, 
   };
   unique_ptr<ICursor2> device_cursor(resolver->Query(
           TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
           "%s = %" PRId32 " AND %s = %d", 
           TableDevice::COLUMN_ID, device_id,
           TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
   if (device_cursor->MoveToNext()) {
     Platform platform = static_cast<Platform>(device_cursor->GetInt32(0));
     if (!IsMobileDevice(platform)) {
       *route_port = device_cursor->GetInt32(1);
       *data_port = device_cursor->GetInt32(2);
       *device_uuid = device_cursor->GetString(3);
       return true;
     }
   }
   return false;
}

bool UpDownloadThread::GetUpDownloadSource() {
  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = {
    TableTree::COLUMN_UUID, TableTree::COLUMN_DEVICE_ID, 
  };
  const char* device_ip_projs[] = {
    TableDeviceIP::COLUMN_IP,
  };

  IssueFindFileAbort abort(this);
  IssueRequests<IssueFindFileRequest> issuse_requests(
      WAIT_RESPONSE_TIMEOUT_IN_S * 1000, &abort);
  {
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %" PRId32 " AND %s = %d AND %s != %d", 
            TableTree::COLUMN_SYNC_ID, sync_id,
            TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL, //todo: should rule out root_moved trees
            TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));
    string remote_route_uri;
    while (tree_cursor->MoveToNext()) {
      int32_t device_id = tree_cursor->GetInt32(1);
      const char *remote_tree_uuid = tree_cursor->GetString(0);
      int32_t remote_route_port, remote_data_port;
      string remote_device_uuid;
      if (GetUpDownloadDeivce(device_id, &remote_device_uuid, 
                              &remote_route_port, &remote_data_port)) {
        unique_ptr<ICursor2> device_ip_cursor(resolver->Query(
                TableDeviceIP::URI, device_ip_projs, 
                ARRAY_SIZE(device_ip_projs), "%s = %d",
                TableDeviceIP::COLUMN_DEVICE_ID, device_id));

        while (device_ip_cursor->MoveToNext()) {
          IssueFindFileRequest *find_file_req = new IssueFindFileRequest(
              device_id, remote_device_uuid, device_ip_cursor->GetString(0), 
              remote_route_port, remote_data_port, remote_tree_uuid);
          MsgFindFileRequest *request = 
              find_file_req->request.mutable_request();
          request->set_device_uuid(Config::device_uuid());
          if (!local_tree_uuid.empty()) {
            request->set_local_tree_uuid(local_tree_uuid);
          }
          request->set_remote_tree_uuid(remote_tree_uuid);
          request->set_sync_uuid(sync_uuid);
          request->set_path(relative_path);
          issuse_requests.IssueOneRequest(find_file_req);
        }
      }
    }
  }

  while (true) {
    IssueFindFileRequest *find_file_request = 
        issuse_requests.RecvNextResponsedRequest();
    if (find_file_request == NULL) {
      issuse_requests.UpdateDeviceInDatabase();
      if (error == ZISYNC_SUCCESS) {
        // the error may have been set in Upload::IsFindFileResponseOK,
        // so we should not overwrite it here.
        // the code is ugly, but I can do nothing
        error = ZISYNC_ERROR_TIMEOUT;
      }
      return false;
    }
    if (!IsFindFileResponseOk(*find_file_request)) {
      continue;
    }
    file_length = find_file_request->response.response().stat().length();
    remote_tree_uuid = find_file_request->remote_tree_uuid;
    StringFormat(
        &remote_device_data_uri, "tcp://%s:%d",
        find_file_request->remote_ip(), find_file_request->remote_data_port);
    return true;
  }
}
  
void UpDownload::CleanUpImp() {
    MutexAuto auto_mutex(&mutex);
    for (auto iter = task_map.begin(); iter != task_map.end(); iter ++) {
      iter->second->Shutdown();
    }
    task_map.clear();
    assert(task_map.empty());
    alloc_task_id = 0;
  }


}  // namespace zs
