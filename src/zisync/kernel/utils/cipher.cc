// Copyright 2014 zisync.com
#include "zisync/kernel/platform/platform.h"

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/md5.h>
#include <openssl/err.h>

#include <cassert>
#include <string>
#include <memory>

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/utils/zmq.h"

namespace zs {

using std::string;

void cipher_zmq_free(void *data, void *hint) {
  free(data);
}

AesCipher::AesCipher() {
  EVP_CIPHER_CTX_init(&ctx_);
  cipher_ = EVP_aes_128_cbc();

  int keysize = EncryptKeyLength();
  key_.resize(keysize);
  int ret = RAND_bytes(reinterpret_cast<unsigned char*>(
      const_cast<char*>(key_.data())), keysize);
  assert(ret == 1);
}

AesCipher::AesCipher(const string& key) {
  EVP_CIPHER_CTX_init(&ctx_);
  cipher_ = EVP_aes_128_cbc();
  assert (EVP_CIPHER_key_length(cipher_) == static_cast<int>(key.size()));
  key_ = key;
}

err_t AesCipher::Encrypt(const ZmqMsg *in, ZmqMsg *out) {
  auto key = reinterpret_cast<const unsigned char*>(key_.data());
  int ret = EVP_EncryptInit_ex(&ctx_, EVP_aes_128_cbc(), NULL, key, NULL);
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_EncryptInit_ex fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    return ZISYNC_ERROR_CIPHER;
  }

  std::unique_ptr<unsigned char> out_data(
      new unsigned char[EncryptMaxSize(in->size())]);
  if (out_data == NULL) {
    ZSLOG_ERROR("new for out_data fail.");
    return ZISYNC_ERROR_MEMORY;
  }
  int out_len, final_len;

  ret = EVP_EncryptUpdate(
      &ctx_, out_data.get(), &out_len,
      static_cast<unsigned char*>(const_cast<void*>(in->data())),
      static_cast<int>(in->size()));
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_EncryptUpdate fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    return ZISYNC_ERROR_CIPHER;
  }

  ret = EVP_EncryptFinal_ex(&ctx_, out_data.get() + out_len, &final_len);
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_EncryptFinal_ex fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    return ZISYNC_ERROR_CIPHER;
  }

  out->SetData(out_data.release(), out_len + final_len, cipher_zmq_free);
  return ZISYNC_SUCCESS;
}

err_t AesCipher::Decrypt(const ZmqMsg *in, ZmqMsg *out) {
  auto key = reinterpret_cast<const unsigned char*>(key_.data());
  int ret = EVP_DecryptInit_ex(&ctx_, cipher_, NULL, key, NULL);
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_DecryptInit_ex() fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    return ZISYNC_ERROR_CIPHER;
  }

  unsigned char *out_data = static_cast<unsigned char*>(
      malloc(EncryptMaxSize(in->size())));
  if (out_data == NULL) {
    ZSLOG_ERROR("malloc for out_data fail.");
    return ZISYNC_ERROR_MEMORY;
  }
  int out_len, final_len;

  ret = EVP_DecryptUpdate(
      &ctx_, out_data, &out_len,
      static_cast<unsigned char*>(const_cast<void*>(in->data())),
      static_cast<int>(in->size()));
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_DecryptUpdate fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    free(out_data);
    return ZISYNC_ERROR_CIPHER;
  }

  ret = EVP_DecryptFinal_ex(&ctx_, out_data + out_len, &final_len);
  if (ret == 0) {
    // ZSLOG_ERROR("EVP_DecryptFinal_ex fail : %s",
    //     ERR_error_string(ERR_get_error(), NULL));
    free(out_data);
    return ZISYNC_ERROR_CIPHER;
  }

  out->SetData(out_data, out_len + final_len, cipher_zmq_free);
  return ZISYNC_SUCCESS;
}

err_t AesCipher::EncryptAesKey(const char *dst_uuid, string *encrypted_key) {
  return ZISYNC_SUCCESS;
}
err_t AesCipher::DecryptAesKey(const string *encrypted_key) {
  return ZISYNC_SUCCESS;
}

err_t RsaCipher::public_encrypt(const string *in, string *out) {
  return ZISYNC_SUCCESS;
}
err_t RsaCipher::public_decrypt(const string *in, string *out) {
  return ZISYNC_SUCCESS;
}
err_t RsaCipher::private_encrypt(const string *in, string *out) {
  return ZISYNC_SUCCESS;
}
err_t RsaCipher::private_decrypt(const string *in, string *out) {
  return ZISYNC_SUCCESS;
}

err_t InitCipher() {
  // CRYPTO_thread_setup();
  OpenSSL_add_all_ciphers();

  return ZISYNC_SUCCESS;
}

void CleanupCipher() {
  CRYPTO_cleanup_all_ex_data();  // generic
  EVP_cleanup();
  ERR_clear_error();
  ERR_free_strings();            // for ERR
  ERR_remove_thread_state(NULL);           // for ERR
}

err_t Signature(const ZmqMsg *msg, string *sign) {
  return ZISYNC_SUCCESS;
}

err_t Verify(const ZmqMsg *msg, const string *sign, const string *src_uuid) {
  return ZISYNC_SUCCESS;
}

string GenAesKey(const char *passphase) {
  string key(AES_KEY_SIZE, '\0');
  int nrounds = 2;
  int ret = EVP_BytesToKey(
      EVP_aes_128_cbc(), EVP_sha1(), NULL, (unsigned char *)passphase, 
      strlen(passphase), nrounds, (unsigned char*)&(*key.begin()), NULL);
  assert(ret == AES_KEY_SIZE);
  return key;
}

}  // namespace zs
