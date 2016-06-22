// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_UTILS_CIPHER_H_
#define ZISYNC_KERNEL_UTILS_CIPHER_H_

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>
#include <cstring>

#include <string>
#include <cassert>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

using std::string;

class ZmqMsg;


const int AES_BLOCK_SIZE = 16;
const int AES_KEY_SIZE = 16;

class AesCipher {
 public:
  // gen a random key
  AesCipher();
  explicit AesCipher(const string& key);

  ~AesCipher() { EVP_CIPHER_CTX_cleanup(&ctx_); }

  std::string& key() { return key_; }
  
  err_t Encrypt(const ZmqMsg *in, ZmqMsg *out);
  err_t Decrypt(const ZmqMsg *in, ZmqMsg *out);
  err_t EncryptAesKey(const char *dst_uuid, string *encrypted_key);
  err_t DecryptAesKey(const string *encrypted_key);

 private:
  AesCipher(AesCipher&);
  void operator=(AesCipher&);

  int EncryptKeyLength() {
    return EVP_CIPHER_key_length(cipher_);
  }
  int EncryptMaxSize(int in_len) {
    return in_len + EVP_CIPHER_block_size(cipher_);
  }

  EVP_CIPHER_CTX ctx_;
  const EVP_CIPHER* cipher_;
  std::string key_;
};

// #define RSA_EXPONENT 3
// #define RSA_KEY_SIZE 2048
// #define RSA_PADDING  RSA_PKCS1_PADDING
//
// #define RSA_ENCRYPT_MAX_SIZE 256
// #define RSA_DECRYPT_MAX_SIZE 256

class RsaCipher {
 public:
  RsaCipher():key(NULL) {}
  ~RsaCipher() {
    if (key != NULL) {
      RSA_free(key);
    }
  }

  err_t public_encrypt(const string *in, string *out);
  err_t public_decrypt(const string *in, string *out);
  err_t private_encrypt(const string *in, string *out);
  err_t private_decrypt(const string *in, string *out);

 private:
  RsaCipher(RsaCipher&);
  void operator=(RsaCipher&);

  RSA*        key;
};

err_t Signature(const ZmqMsg *msg, string *sign);
err_t Verify(const ZmqMsg *msg, const string *sign, const string *src_uuid);

err_t InitCipher();
void CleanupCipher();
string GenAesKey(const char *passphase);

// string RsaPubPem2Der(string sPem);
// string RsaPubDer2Pem(string sDer);
// string RsaPriPem2Der(string sPem);
// string RsaPriDer2Pem(string sDer);
//
// BOOL ZiSyncDigest(CRSA* lpRSA, const char* lpData,
// int nSize, string& sResult);
// BOOL ZiSyncVerify(CRSA* lpRSA, string sResult,
// const char* lpData, int nSize);
//
}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_CIPHER_H_
