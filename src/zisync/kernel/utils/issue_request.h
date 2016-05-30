// Copyright 2014, zisync.com

#ifndef ZISYNC_KERNEL_UTILS_ISSUE_REQUEST_H_
#define ZISYNC_KERNEL_UTILS_ISSUE_REQUEST_H_

#include <zmq.h>

#include <stdint.h>
#include <vector>
#include <memory>
#include <string>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/response.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"

namespace zs {

class ZmqSocket;
class Response;
class Request;

class IssueRequestsAbort {
 public:
  virtual bool IsAborted() = 0;
};

class IssueRequestsKernelAbort : public IssueRequestsAbort {
 public:
  virtual bool IsAborted() {
    return zs::IsAborted();
  }
};

IssueRequestsAbort* GetDefaultAbort();

class IssueRequest {
 public:
  IssueRequest(
      int32_t remote_device_id, const string &remote_device_uuid,
      const char *remote_ip, int32_t remote_route_port, 
      bool is_encrypt_with_token):
      socket_(NULL), error_(ZISYNC_SUCCESS), 
      remote_device_id_(remote_device_id), remote_ip_(remote_ip), 
      remote_device_uuid_(remote_device_uuid), 
      remote_route_port_(remote_route_port), 
      is_encrypt_with_token_(is_encrypt_with_token) {}
  IssueRequest(
      int32_t remote_device_id, const char *remote_ip, 
      int32_t remote_route_port, bool is_encrypt_with_token):
      socket_(NULL), error_(ZISYNC_SUCCESS), 
      remote_device_id_(remote_device_id), 
      remote_ip_(remote_ip), remote_route_port_(remote_route_port), 
      is_encrypt_with_token_(is_encrypt_with_token) {}
  ~IssueRequest();

  virtual Request* mutable_request() = 0;
  virtual Response* mutable_response() = 0;
  ZmqSocket *socket() { return socket_; }
  virtual err_t SendRequest() {
    if (is_encrypt_with_token_) {
      return mutable_request()->SendToWithAccountEncrypt(*socket_);
    } else {
      return mutable_request()->SendTo(*socket_);
    }
  }
  virtual err_t RecvResponse() {
    if (is_encrypt_with_token_) {
      error_ = mutable_response()->RecvFromWithAccountEncrypt(
          *socket_, 0, &recv_remote_device_uuid_);
    } else {
      error_ = mutable_response()->RecvFrom(
          *socket_, &recv_remote_device_uuid_);
    }
    if (error_ != ZISYNC_SUCCESS) {
      ZSLOG_WARNING("Recv response fail : %s", 
                    zisync_strerror(error_));
    }
    UpdateDeviceInDatabaseWithRecvResp();
    return error_;
  }
  void InitSocket(const ZmqContext &context) {
    socket_ = new ZmqSocket(context, ZMQ_REQ);
  }
  int32_t remote_device_id() const { return remote_device_id_; }
  const char* remote_ip() const { return remote_ip_.c_str(); }
  int32_t remote_route_port() const { return remote_route_port_; }
  bool is_encrypt_with_token() const { return is_encrypt_with_token_; }
  void set_error(err_t error) { error_ = error; }
  err_t error() const { return error_; }
  const string& remote_device_uuid() { return remote_device_uuid_; }
  const string& recv_remote_device_uuid() { return recv_remote_device_uuid_; }

 protected:
  ZmqSocket *socket_;
  err_t error_;
 private:
  IssueRequest(IssueRequest&);
  void operator=(IssueRequest&);
  void UpdateDeviceInDatabaseWithRecvResp();

  int32_t remote_device_id_;
  std::string remote_ip_, remote_device_uuid_, recv_remote_device_uuid_;
  int32_t remote_route_port_;
  bool is_encrypt_with_token_;
};
  
class IssueFindRequest : public IssueRequest {
 public:
  IssueFindRequest(
      int32_t sync_id_, const char *remote_ip,
      int32_t remote_route_port, const char *remote_tree_uuid_):
      IssueRequest(-1, remote_ip, remote_route_port, false),
      sync_id(sync_id_), remote_tree_uuid(remote_tree_uuid_) {}
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }

  int32_t sync_id;
  const string remote_tree_uuid;
  FindRequest request;
  FindResponse response;
 private:
  IssueFindRequest(IssueFindRequest&);
  void operator=(IssueFindRequest&);
};

class IssueRemoveRemoteFileRequest : public IssueRequest {
 public:
  IssueRemoveRemoteFileRequest(
      const char *remote_ip, int32_t remote_route_port):
      IssueRequest(-1, remote_ip, remote_route_port, false){}
  virtual Request* mutable_request() { return &request; }
  virtual Response* mutable_response() { return &response; }

  RemoveRemoteFileRequest request;
  RemoveRemoteFileResponse response;
 private:
  IssueRemoveRemoteFileRequest(IssueRemoveRemoteFileRequest&);
  void operator=(IssueRemoveRemoteFileRequest&);
};

template <typename IssueRequest>
class IssueRequests {
 public:
  IssueRequests(
      int32_t timeout_in_ms, IssueRequestsAbort *abort_ = GetDefaultAbort()):
      items(NULL), recv_response_num(0),
      timeout_in_ms_(timeout_in_ms), abort(abort_) {}
  ~IssueRequests() {
    requests_.clear();
    if (items != NULL) {
      delete items;
    }
  }

  /*  the IssuseRequests free the issue_request */
  void IssueOneRequest(IssueRequest *issue_request);
  /*  should not call IssueOneRequest after GetNextResponsedRequest */
  IssueRequest* RecvNextResponsedRequest();
  const char* GetRemoteIpString(std::string* ip);
  void UpdateDeviceInDatabase();
  const std::vector<std::unique_ptr<IssueRequest>>& requests() const {
    return requests;
  }

 private:
  IssueRequests(IssueRequests&);
  void operator=(IssueRequests&);
  std::vector<std::unique_ptr<IssueRequest>> requests_;

  zmq_pollitem_t *items;
  unsigned int recv_response_num;
  int32_t timeout_in_ms_;
  int64_t expire_time_in_ms;
  IssueRequestsAbort *abort;
};

template <typename IssueRequest>
void IssueRequests<IssueRequest>::IssueOneRequest(IssueRequest *request) {
  assert(items == NULL);
  assert(request->socket() == NULL);
  request->InitSocket(GetGlobalContext());
  string remote_route_uri;
  StringFormat(&remote_route_uri, "tcp://%s:%d", request->remote_ip(), 
               request->remote_route_port());
  err_t zisync_ret = request->socket()->Connect(remote_route_uri.c_str());
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = request->SendRequest();
  assert(zisync_ret == ZISYNC_SUCCESS);
  requests_.emplace_back(request);
}

template <typename IssueRequest>
IssueRequest* IssueRequests<IssueRequest>::RecvNextResponsedRequest() {
  if (requests_.size() == 0) {
    return NULL;
  }
  if (items == NULL) {
    items = new zmq_pollitem_t[requests_.size()];
    for (unsigned int i = 0; i < requests_.size(); i ++) {
      items[i].fd = -1;
      items[i].socket = requests_[i]->socket()->socket();
      items[i].events = ZMQ_POLLIN;
      items[i].revents = 0;
    }
    expire_time_in_ms = OsTimeInMs() + timeout_in_ms_;
  }
  while (recv_response_num < requests_.size()) {
    int32_t request_size = static_cast<int32_t>(requests_.size());
    int ret = 0;
    if (timeout_in_ms_ == -1) {
      while (ret == 0) {
        ret = zmq_poll(items, request_size, 1000);
      }
    } else {
      int32_t timeout_in_ms = static_cast<int32_t>(
          expire_time_in_ms - OsTimeInMs());
      if (timeout_in_ms < 0) {
        timeout_in_ms = 0;
      }
      while (timeout_in_ms >= 0) {
        if (abort->IsAborted()) {
          return NULL;
        }
        ret = zmq_poll(
            items, request_size, timeout_in_ms < 1000 ? 
            timeout_in_ms : 1000);
        if (ret != 0) {
          break;
        }
        timeout_in_ms -= 1000;
      }
    }
    if (ret == 0) {
      return NULL;
    } else if (ret == -1) {
      ZSLOG_WARNING("zmq_poll return -1 continue poll.");
      continue;
    }
    for (unsigned int i = 0; i < requests_.size(); i ++) {
      if (items[i].revents & ZMQ_POLLIN) {
        recv_response_num ++;
        items[i].events = 0;
        items[i].revents = 0;
        requests_[i]->RecvResponse();
        return requests_[i].get();
      }
    }
  }
  return NULL;
}

template <typename IssueRequest>
const char* IssueRequests<IssueRequest>::GetRemoteIpString(std::string* ip) {
  ip->append(1, '[');
  for (size_t i = 0; i < requests_.size(); i ++) {
    if (i == 0) {
      StringAppendFormat(ip, "%s:%d", requests_[i]->remote_ip(),
                         requests_[i]->remote_route_port());
    } else {
      StringAppendFormat(ip, ", %s:%d", requests_[i]->remote_ip(),
                         requests_[i]->remote_route_port());
    }
  }
  ip->append(1, ']');

  return ip->data();
}

/*  call if wait util return NULL */
template <typename IssueRequest>
void IssueRequests<IssueRequest>::UpdateDeviceInDatabase() {
  for (unsigned int i = 0; i < requests_.size(); i ++) {
    if (items[i].events != 0) { // recv resp
      UpdateEarliestNoRespTimeInDatabase(
          requests_[i]->remote_device_id(), requests_[i]->remote_ip(), 
          OsTimeInS());
    }
  }
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_ISSUE_REQUEST_H_
