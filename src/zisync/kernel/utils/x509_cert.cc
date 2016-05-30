/**
 * @file x509.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief wrapper of x509 function.
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

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/x509_cert.h"

namespace zs {

static inline unsigned char* ToUnsigedCstr(const std::string& s) {
  return const_cast<unsigned char*>(
      reinterpret_cast<const unsigned char*>(s.data()));
}

/* Generates a 2048-bit RSA key. */
static EVP_PKEY* GenerateKey() {
  /* Allocate memory for the EVP_PKEY structure. */
  EVP_PKEY* pkey = EVP_PKEY_new();
  if(!pkey) {
    ZSLOG_ERROR("Unable to create EVP_PKEY structure.");
    return NULL;
  }
    
  /* Generate the RSA key and assign it to pkey. */
  RSA * rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
  if(!EVP_PKEY_assign_RSA(pkey, rsa)) {
    ZSLOG_ERROR("Unable to generate 2048-bit RSA key.");
    EVP_PKEY_free(pkey);
    return NULL;
  }
    
  /* The key has been generated, return it. */
  return pkey;
}

X509Cert::X509Cert()
    : x509_(NULL), pkey_(NULL) {
}

X509Cert::~X509Cert() {
  if (x509_ != NULL) {
    X509_free(x509_);
    x509_ = NULL;
  }

  if (pkey_ != NULL) {
    EVP_PKEY_free(pkey_);
    pkey_ = NULL;
  }
}

err_t X509Cert::Create(const std::string& c,
                       const std::string& st,
                       const std::string& l,
                       const std::string& o,
                       const std::string& ou,
                       const std::string& cn) {
  pkey_ = GenerateKey();
  if (pkey_ == NULL) {
    assert(false);
    return ZISYNC_ERROR_SSL;
  }
  
  /* Allocate memory for the X509 structure. */
  X509* x509 = X509_new();
  if(!x509) {
    assert(x509);
    ZSLOG_ERROR("Unable to create X509 structure.");
    return ZISYNC_ERROR_MEMORY;
  }
    
  int serial = rand() % 1048576;
  
  /* Set the serial number. */
  ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);
    
  /* Set the public key for our certificate. */
  X509_set_pubkey(x509, pkey_);
    
  /* We want to copy the subject name to the issuer name. */
  X509_NAME * name = X509_get_subject_name(x509);
    
  /* Set the country code and common name. */
  X509_NAME_add_entry_by_txt(
      name, "C", MBSTRING_ASC, ToUnsigedCstr(c), -1, -1, 0);
  X509_NAME_add_entry_by_txt(
      name, "ST", MBSTRING_ASC, ToUnsigedCstr(st), -1, -1, 0);
  X509_NAME_add_entry_by_txt(
      name, "L", MBSTRING_ASC, ToUnsigedCstr(l), -1, -1, 0);
  X509_NAME_add_entry_by_txt(
      name, "O", MBSTRING_ASC, ToUnsigedCstr(o), -1, -1, 0);
  X509_NAME_add_entry_by_txt(
      name, "OU", MBSTRING_ASC, ToUnsigedCstr(ou), -1, -1, 0);
  X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_ASC, ToUnsigedCstr(cn), -1, -1, 0);

  x509_ = x509;
  
  return ZISYNC_SUCCESS;
}

err_t X509Cert::AddExt(X509* x509, int nid, const char* value) {
  X509_EXTENSION *ex;
  X509V3_CTX ctx;
  /* This sets the 'context' of the extensions. */
  /* No configuration database */
  X509V3_set_ctx_nodb(&ctx);
  /* Issuer and subject certs: both the target since it is self signed,
   * no request and no CRL
   */
  X509V3_set_ctx(&ctx, x509, x509, NULL, NULL, 0);
  ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, const_cast<char*>(value));
  if (!ex)
    return ZISYNC_ERROR_SSL;

  X509_add_ext(x509,ex,-1);
  X509_EXTENSION_free(ex);

  return ZISYNC_SUCCESS;
}

err_t X509Cert::PemWriteToFile(const std::string& key_pem, const std::string& x509_pem) {
  /* Open the PEM file for writing the key to disk. */
  FILE * pkey_file = fopen(key_pem.c_str(), "wb");
  if(!pkey_file) {
    ZSLOG_ERROR("Unable to open \"key.pem\" for writing.");
    return ZISYNC_ERROR_OS_IO;
  }

  /* Write the key to disk. */
  int ret = PEM_write_PrivateKey(pkey_file, pkey_, NULL, NULL, 0, NULL, NULL);
  fclose(pkey_file);

  if (!ret) {
    ZSLOG_ERROR("Unable to write private key to disk.");
    return ZISYNC_ERROR_SSL;
  }
  
  /* Open the PEM file for writing the certificate to disk. */
  FILE * x509_file = fopen(x509_pem.c_str(), "wb");
  if(!x509_file) {
    ZSLOG_ERROR("Unable to open \"%s\" for writing.", x509_pem.c_str());
    return ZISYNC_ERROR_OS_IO;
  }
    
  /* Write the certificate to disk. */
  X509_print_fp(x509_file, x509_);
  ret = PEM_write_X509(x509_file, x509_);
  fclose(x509_file);
    
  if(!ret) {
    ZSLOG_ERROR("Unable to write certificate to disk.");
    return ZISYNC_ERROR_SSL;
  }
    
  return ZISYNC_SUCCESS;
}

err_t X509Cert::PrintToFile(FILE* key_fp, FILE* x509_fp) {
  /* Write the key to disk. */
  if (key_fp != NULL) {
    BIO* bio = BIO_new_fp(key_fp, BIO_NOCLOSE);
    int ret = EVP_PKEY_print_private(bio, pkey_, 4, NULL);
    BIO_free(bio);

    assert(ret == 1);
  }

  /* Write the certificate to disk. */
  if (x509_fp != NULL) {
    int ret = X509_print_fp(x509_fp, x509_);
    assert(ret == 1);
  }
  
  return ZISYNC_SUCCESS;
}

err_t X509Cert::PemAppendX509ToString(std::string* blob) {
  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio) {
    ZSLOG_ERROR("Error create in-memory bio.");
    return ZISYNC_ERROR_MEMORY;
  }
  
  int ret = PEM_write_bio_X509(bio, x509_);
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
  blob->append(pp, nbytes);

  BIO_free(bio);
  
  return ZISYNC_SUCCESS;
}


err_t X509Cert::SignWith(
    EVP_PKEY* pkey_ca, X509* x509_ca, bool with_sign_permission) {
  /* This certificate is valid from now until exactly ten year from now. */
  X509_gmtime_adj(X509_get_notBefore(x509_), 0);
  X509_gmtime_adj(X509_get_notAfter(x509_), 315360000L);
    
  /* Now set the issuer name. */
  X509_NAME * name = X509_get_subject_name(x509_ca);
  X509_set_issuer_name(x509_, name);

  /* add verious extensions */
  if (with_sign_permission) {
    AddExt(x509_, NID_basic_constraints, "CA:TRUE");    
  }
  // add_ext(x, NID_key_usage, "critical,keyCertSign,cRLSign");
  // add_ext(x, NID_subject_key_identifier, "hash");

  /* Some Netscape specific extensions */
  // add_ext(x, NID_netscape_cert_type, "sslCA");
  // add_ext(x, NID_netscape_comment, "example comment extension");
    
  /* Actually sign the certificate with our key. */
  if(!X509_sign(x509_, pkey_ca, EVP_sha1())) {
    assert(false);
    ZSLOG_ERROR("Error signing certificate.");
    return ZISYNC_ERROR_SSL;
  }

  return ZISYNC_SUCCESS;  
}

}  // namespace zs

 
 



