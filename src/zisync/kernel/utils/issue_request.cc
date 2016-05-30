// Copyright 2014, zisync.com
#include <string>
#include <cassert>

#include "zisync/kernel/utils/issue_request.h"
#include "zisync/kernel/utils/response.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/database/icore.h"

namespace zs {

using std::string;

IssueRequest::~IssueRequest() {
  if (socket_ != NULL) {
    delete socket_;
  }
}

static IssueRequestsKernelAbort default_abort;

IssueRequestsAbort* GetDefaultAbort() {
  return &default_abort;
}

void IssueRequest::UpdateDeviceInDatabaseWithRecvResp() {
  if (!remote_device_uuid_.empty() && !recv_remote_device_uuid_.empty() &&
      remote_device_uuid_ != recv_remote_device_uuid_) {
    // delete the device_ip
    IContentResolver *resolver = GetContentResolver();
    resolver->Delete(
        TableDeviceIP::URI, " %s = %d AND %s = '%s'" ,
        TableDeviceIP::COLUMN_DEVICE_ID, remote_device_id_,
        TableDeviceIP::COLUMN_IP, remote_ip_.c_str());
    ZSLOG_INFO("Device(%d) 's IP(%s) delete due to uuid not match",
               remote_device_id_, remote_ip_.c_str());
    error_ = ZISYNC_ERROR_GENERAL;
  } else {
    UpdateEarliestNoRespTimeInDatabase(
        remote_device_id_, remote_ip_.c_str(), 
        TableDeviceIP::EARLIEST_NO_RESP_TIME_NONE);
    if (error_ == ZISYNC_ERROR_PERMISSION_DENY && 
        remote_device_id_ != -1 && is_encrypt_with_token_) {
      assert(!remote_device_uuid_.empty());
      // for old version msg, uuid is set with "", so is impossible to check
      // uuid
      RemoteDeviceTokenChangeToDiff(remote_device_id_, remote_route_port_);
    }
  }
}

}  // namespace zs
