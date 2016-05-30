/**
 * @file x509_cert.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief x509 function wrapper.
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

#ifndef ZISYNC_KERNEL_UTILS_X509_CERT_H_
#define ZISYNC_KERNEL_UTILS_X509_CERT_H_

#include <openssl/x509.h>

#include <string>

#include "zisync_kernel.h"

namespace zs {

class X509Cert {
 public:
  X509Cert();
  ~X509Cert();

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

  // generate a new x509 cert
  err_t Create(const std::string& c,
               const std::string& st,
               const std::string& l,
               const std::string& o,
               const std::string& ou,
               const std::string& cn);
  // unmashling a x509 cert from memory blob
  err_t LoadFromString(const std::string& key_blob,
                       const std::string& x509_blob);

  err_t PrintToFile(FILE* key_fp, FILE* x509_fp);
  err_t PemWriteToFile(const std::string& key_pem,
                  const std::string& x509_pem);
  err_t PemAppendX509ToString(std::string* blob);

  err_t SignWith(EVP_PKEY* pkey_ca,
                 X509* x509_ca,
                 bool with_sign_permission);
  err_t SelfSign() {
    return SignWith(pkey_, x509_, true);
  }
  
 protected:
  err_t AddExt(X509* x509, int nid, const char* value);

 private:
  X509* x509_;
  EVP_PKEY* pkey_;

};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_X509_CERT_H_
