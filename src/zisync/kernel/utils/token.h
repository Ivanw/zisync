/**
 * @file token_service.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Token Service.
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

#ifndef ZISYNC_KERNEL_TOKEN_SERVICE_H_
#define ZISYNC_KERNEL_TOKEN_SERVICE_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class ZmqContext;

class CertInfo {
 public:
  std::string uuid;
  std::string cert;
};

class ITokenDataProvider {
 public:
  virtual ~ITokenDataProvider() {}

  // Query database to load cert informaiton
  virtual void QueryCertInfo(const std::string& uuid,
                             std::vector<CertInfo>* certs) = 0;
};

class ITokenService {
 public:
  virtual ~ITokenService() {
    /* virtual destructor */
  }

  virtual err_t Initialize(const ZmqContext& zmq_context,
                           ITokenDataProvider* provider) = 0;
  virtual err_t CleanUp() = 0;
  virtual const std::string GetSendToken(const std::string& uuid) = 0;
  virtual const std::string GetRecvToken(const std::string& uuid) = 0;

  virtual void UpdateSendToken(const std::string& uuid) = 0;
  virtual void UpdateRecvToken(const std::string& uuid, const std::string& new_token) = 0;

 protected:
  ITokenService()  {
  }

 private:
  ITokenService(ITokenService&);
  void operator=(ITokenService&);
};

// singleton
ITokenService* GetTokenService();


}  // namespace zs

#endif  // ZISYNC_KERNEL_TOKEN_SERVICE_H_
