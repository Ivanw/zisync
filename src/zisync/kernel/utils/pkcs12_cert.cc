/**
 * @file pkcs12_cert.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief pkcs12 function wraaper.
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

#include <openssl/err.h>
#include <openssl/pem.h>

#include <cassert>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/pkcs12_cert.h"


namespace zs {

static EVP_PKEY* EVP_PKEY_dup(EVP_PKEY* pkey) {
  CRYPTO_add(&pkey->references,1,CRYPTO_LOCK_EVP_PKEY);
  return pkey;
}

Pkcs12Cert::Pkcs12Cert()
    : pkcs12_(NULL), x509_(NULL), pkey_(NULL), chain_(NULL) {
}

Pkcs12Cert::~Pkcs12Cert() {
  if (pkcs12_) {
    PKCS12_free(pkcs12_);
    pkcs12_ = NULL;
  }

  if (x509_) {
    X509_free(x509_);
    x509_ = NULL;
  }

  if (pkey_) {
    EVP_PKEY_free(pkey_);
    pkey_ = NULL;
  }

  if (chain_) {
    sk_X509_pop_free(chain_, X509_free);
    chain_ = NULL;
  }
}

err_t Pkcs12Cert::Create(EVP_PKEY* pkey, X509* cert, STACK_OF(X509)* ca) {
  pkey_ = EVP_PKEY_dup(pkey);
  x509_ = X509_dup(cert);
  if (ca) {
    chain_ = sk_X509_dup(ca);
  } else {
    chain_ = NULL;
  }

  char pass[] = "zisync";
  char name[] = "zisync";
  PKCS12* pkcs12 = PKCS12_create(pass, name, pkey, cert, chain_, 0, 0, 0, 0, 0);
  if (pkcs12 == NULL) {
    ZSLOG_ERROR("Error creating PKCS#12 structure\n");
    return ZISYNC_ERROR_GENERAL;
  } else {
    pkcs12_ = pkcs12;
    return ZISYNC_SUCCESS;
  }
}


err_t Pkcs12Cert::LoadFromFile(const std::string& path) {
  FILE *fp;
  PKCS12 *p12;

  if (!(fp = fopen(path.c_str(), "rb"))) {
    ZSLOG_ERROR("Error opening file %s\n", path.c_str());
    return ZISYNC_ERROR_OS_IO;
  }
  
  p12 = d2i_PKCS12_fp(fp, NULL);
  fclose (fp);
  
  if (!p12) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error reading PKCS#12 file:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    return ZISYNC_ERROR_SSL;
  }
  
  if (!PKCS12_parse(p12, "zisync", &pkey_, &x509_, &chain_)) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error parsing PKCS#12 file:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    PKCS12_free(p12);
    return ZISYNC_ERROR_SSL;
  }
  
  pkcs12_ = p12;

  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::LoadFromString(const std::string& blob) {
  PKCS12 *p12;
  BIO* bio = BIO_new_mem_buf(
      const_cast<char*>(blob.data()), blob.size());
  
  p12 = d2i_PKCS12_bio(bio, NULL);
  if (!p12) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error reading PKCS#12 blob:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    return ZISYNC_ERROR_SSL;
  }
  
  if (!PKCS12_parse(p12, "zisync", &pkey_, &x509_, &chain_)) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error parsing PKCS#12 file:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    PKCS12_free(p12);
    return ZISYNC_ERROR_SSL;
  }
  
  pkcs12_ = p12;

  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::StoreToFile(const std::string& path) {
  FILE *fp;

  if (!(fp = fopen(path.c_str(), "wb"))) {
    ZSLOG_ERROR("Error opening file %s\n", path.c_str());
    return ZISYNC_ERROR_OS_IO;
  }
  
  int ret = i2d_PKCS12_fp(fp, pkcs12_);
  fclose (fp);
  
  if (ret != 1) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error reading PKCS#12 file:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    return ZISYNC_ERROR_SSL;
  }
  
  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::StoreToString(std::string* blob) {
  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    return ZISYNC_ERROR_MEMORY;
  }
  
  int ret = i2d_PKCS12_bio(bio, pkcs12_);
  if (ret != 1) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error reading PKCS#12 file:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    BIO_free(bio);
    return ZISYNC_ERROR_SSL;
  }

  char* pp;
  int nbytes = BIO_get_mem_data(bio, &pp);
  assert (nbytes >= 0);
  blob->assign(pp, nbytes);

  BIO_free(bio);
  
  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::StorePubkeyChain(std::string* blob) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    return ZISYNC_ERROR_MEMORY;
  }
  
  for(int i = 0; i < sk_X509_num(chain_); i++) {
    PEM_write_bio_X509(bio, sk_X509_value(chain_, i));
  }

  char* pp;
  int nbytes = BIO_get_mem_data(bio, &pp);
  assert(nbytes >= 0);
  blob->assign(pp, nbytes);
  
  BIO_free(bio);

  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::StoreClientCert(std::string* blob) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    return ZISYNC_ERROR_MEMORY;
  }
  
  PEM_write_bio_X509(bio, x509_);

  char* pp;
  int nbytes = BIO_get_mem_data(bio, &pp);
  assert(nbytes >= 0);
  blob->assign(pp, nbytes);
  
  BIO_free(bio);

  return ZISYNC_SUCCESS;
}

err_t Pkcs12Cert::Verify(const std::string& client_x509,
                         const std::string& signer_chain) {
  err_t zisync_ret = ZISYNC_SUCCESS;
  BIO* bio = NULL;
  X509* x509= NULL;
  X509* cert = NULL;
  X509_STORE* ctx = NULL;
  X509_STORE_CTX* csc = NULL;
  STACK_OF(X509)* sk = NULL;
  
  bio = BIO_new_mem_buf(
      const_cast<char*>(client_x509.data()), client_x509.size());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    zisync_ret = ZISYNC_ERROR_MEMORY;
    goto end;
  }

  x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  if (!x509) {
    ZSLOG_ERROR("Error PEM_read_bio_X509: %s", client_x509.c_str());
    zisync_ret = ZISYNC_ERROR_INVALID_FORMAT;
    goto end;
  }
  BIO_free(bio);

  bio = BIO_new_mem_buf(
      const_cast<char*>(signer_chain.data()), signer_chain.size());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    zisync_ret = ZISYNC_ERROR_MEMORY;
    goto end;
  }

  sk = sk_X509_new_null();
  while(BIO_eof(bio) != 1) {
    cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (!cert) {
      ZSLOG_ERROR("Error PEM_read_bio_X509: %s", signer_chain.c_str());
      zisync_ret = ZISYNC_ERROR_INVALID_FORMAT;
      goto end;
    }
    sk_X509_push(sk, cert);
  }

  ctx = X509_STORE_new();
  if (ctx == NULL) {
    ZSLOG_ERROR("Error create in-memory X509_STORE.");
    zisync_ret = ZISYNC_ERROR_MEMORY;
    goto end;
  }

  if (X509_STORE_add_cert(ctx, x509_) <= 0) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error X509_STORE_add_cert:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    zisync_ret = ZISYNC_ERROR_SSL;
    goto end;
  }
  
  csc = X509_STORE_CTX_new();
  if (csc == NULL) {
    ZSLOG_ERROR("Error create X509_STORE_CTX.");
    zisync_ret = ZISYNC_ERROR_MEMORY;
    goto end;
  }

  // X509_STORE_set_flags(ctx, vflags);
  if(!X509_STORE_CTX_init(csc,ctx, x509, sk)) {
    unsigned long eno = ERR_peek_last_error();
    ZSLOG_ERROR("Error X509_STORE_CTX_init:%lu:%s",
                eno, ERR_error_string(eno, NULL));
    return ZISYNC_ERROR_SSL;
  }

  if(chain_) {
    X509_STORE_CTX_trusted_stack(csc, chain_);
  }
  // if (crls) {
  //   X509_STORE_CTX_set0_crls(csc, crls);    
  // }

  if (X509_verify_cert(csc) <= 0) {
    zisync_ret = ZISYNC_ERROR_UNTRUSTED;
  }

end:
  if (bio) {
    BIO_free(bio);
  }

  if (x509) {
    X509_free(x509);
  }

  if (sk) {
    sk_X509_pop_free(sk, X509_free);
  }

  if (csc) {
    X509_STORE_CTX_free(csc);    
  }

  if (ctx) {
    X509_STORE_free(ctx);
  }
  
  return zisync_ret;
}

}  // namespace zs



