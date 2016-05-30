/**
 * @file pkcs12_cert.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief pkcs12 certificate wrapper.
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

#ifndef ZISYNC_KERNEL_UTILS_PKCS12_CERT_H_
#define ZISYNC_KERNEL_UTILS_PKCS12_CERT_H_

#include <openssl/evp.h>
#include <openssl/pkcs12.h>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class Pkcs12Cert {
 public:
  Pkcs12Cert();
  ~Pkcs12Cert();

  err_t Create(EVP_PKEY* pkey, X509* x509, STACK_OF(X509)* ca);
  err_t LoadFromString(const std::string& blob);
  err_t LoadFromFile(const std::string& path);

  err_t StoreToFile(const std::string& path);
  err_t StoreToString(std::string* blob);

  err_t StorePubkeyChain(std::string* blob);
  err_t StoreClientCert(std::string* blob);

  err_t Verify(const std::string& client_x509, const std::string& signer_chain);

  PKCS12* pkcs12() {
    return pkcs12_;
  }

  X509* x509() {
    return x509_;
  }

  EVP_PKEY* pkey() {
    return pkey_;
  }

  EVP_PKEY* pubkey() {
    if (x509_) {
      return X509_get_pubkey(x509_);
    }
    return NULL;
  }

  STACK_OF(X509)* chain() {
    return chain_;
  }
  
 private:
  PKCS12* pkcs12_;
  X509* x509_;
  EVP_PKEY* pkey_;
  STACK_OF(X509)* chain_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_PKCS12_CERT_H_
