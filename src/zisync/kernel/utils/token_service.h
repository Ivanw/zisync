/**
 * @file token_service.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Token server definition.
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

#ifndef ZISYNC_KERNEL_UTILS_TOKEN_SERVICE_H_
#define ZISYNC_KERNEL_UTILS_TOKEN_SERVICE_H_

#include <unordered_map>
#include "zisync/kernel/utils/token.h"

namespace zs {

class OsMutex;
class OsTimer;

class TokenService : public ITokenService , public IOnTimer {
 public:
  TokenService() : timer_(NULL), mutex_(NULL) {
  }
  virtual ~TokenService() {
    assert(timer_ == NULL);
    assert(mutex_ == NULL);
  }

  virtual err_t Initialize(const ZmqContext& zmq_context,
                           ITokenDataProvider* provider);
  virtual err_t CleanUp();
  virtual const std::string GetSendToken(const std::string& uuid);
  virtual const std::string GetRecvToken(const std::string& uuid);
  virtual void UpdateSendToken(const std::string& uuid);
  virtual void UpdateRecvToken(const std::string& uuid,
                               const std::string& new_token);

  // Send a message to inner_worker to update token used
  // by communicat with remote @param uuid.
  // Set @param uuid to "*" if you want update all the
  // token used for communicate.
  void IssueUpdateSendToken(const std::string& uuid);

  //
  // Implment IOnTimer
  //
  virtual void OnTimer();
  
 private:
  TokenService(TokenService&);
  void operator=(TokenService&);

  virtual void UpdateSendTokenInternal(const CertInfo& cert_info);
  
  const ZmqContext*   context_;
  ITokenDataProvider* provider_;
  OsTimer* timer_;
  OsMutex* mutex_;
  std::unordered_map<std::string, std::string> send_token_table;
  std::unordered_map<std::string, std::string> recv_token_table;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_TOKEN_SERVICE_H_
