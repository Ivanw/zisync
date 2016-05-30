/**
 * @file token_service.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Token Service implmentation.
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
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/token_service.h"

namespace zs {

TokenService s_token_service;

// singleton
ITokenService* GetTokenService() {
  return &s_token_service;
}

err_t TokenService::Initialize(const ZmqContext& zmq_context, ITokenDataProvider* provider) {
  context_ = &zmq_context;
  provider_ = provider;
  
  mutex_ = new OsMutex();
  assert(mutex_);
  mutex_->Initialize();

  timer_ = new OsTimer(300000, this);
  assert(timer_);
  int ret = timer_->Initialize();
  assert(ret == 0);
  
  return ZISYNC_SUCCESS;
}

err_t TokenService::CleanUp() {
  assert(timer_ != NULL);
  timer_->CleanUp();
  delete timer_;
  timer_ = NULL;
  
  assert(mutex_ != NULL);

  mutex_->CleanUp();
  delete mutex_;
  mutex_ = NULL;
  
  return ZISYNC_SUCCESS;
}

const std::string TokenService::GetSendToken(const std::string& uuid) {
  MutexAuto autor(mutex_);
  auto it = send_token_table.find(uuid);
  if (it != send_token_table.end()) {
    return it->second;
  } else {
    IssueUpdateSendToken(uuid);
    return "";
  }
}

const std::string TokenService::GetRecvToken(const std::string& uuid) {
  MutexAuto autor(mutex_);
  auto it= recv_token_table.find(uuid);
  if(it != recv_token_table.end()) {
    return it->second;
  }
  return "";
}

void TokenService::UpdateSendToken(const std::string& uuid) {
  std::vector<CertInfo> certs;
  provider_->QueryCertInfo(uuid, &certs);
  if (uuid != "*" && certs.size() != 1) {
    ZSLOG_ERROR("not cert found for uuid: %s", uuid.data());
  } 

  for (auto it = certs.begin(); it != certs.end(); ++it) {
    UpdateSendTokenInternal(*it);
  }
}

void TokenService::UpdateSendTokenInternal(const CertInfo& cert_info) {
  MutexAuto autor(mutex_);
  // send_token_table[uuid] = new_token;
  
}

void TokenService::UpdateRecvToken(const std::string& uuid,
                                   const std::string& new_token) {
  MutexAuto autor(mutex_);
  recv_token_table[uuid] = new_token;
}

void TokenService::IssueUpdateSendToken(const std::string& uuid) {
  IssueUpdateTokenRequest request;
  request.mutable_request()->set_uuid(uuid);

  ZmqSocket req_socket(*context_, ZMQ_REQ);
  err_t zisync_ret = req_socket.Connect(router_inner_fronter_uri);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Connet to inner fronter fail : %s",
                zisync_strerror(zisync_ret));
    return;
  }

  zisync_ret = request.SendTo(req_socket);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Send IssueUpdateTokenRequest fail : %s",
                zisync_strerror(zisync_ret));
    return;
  }
}

void TokenService::OnTimer() {
  IssueUpdateSendToken(std::string("*"));
}

}  // namespace zs
