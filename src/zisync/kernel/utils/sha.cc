// Copyright 2014, zisync.com

#include <string>
#include <cassert>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/abort.h"

namespace zs {

using std::string;


err_t Sha1Bin(const string& in, string *out) {
  SHA_CTX c;
  out->resize(SHA_DIGEST_LENGTH);
  unsigned char* sha1 = (unsigned char*)(&(*out->begin()));  // NOLINT

  int ret = SHA1_Init(&c);
  assert(ret == 1);
  ret = SHA1_Update(&c, in.c_str(), in.length());
  assert(ret == 1);
  ret = SHA1_Final(sha1, &c);
  assert(ret == 1);

  return ZISYNC_SUCCESS;
}

static char hex_table[] = "0123456789abcdef";

void Sha1MdToHex(unsigned char *md, string *hex) {
    int i;
    char hex_[41];
    for (i = 0; i < SHA_DIGEST_LENGTH; i ++) {
        hex_[2 * i] = hex_table[(md[i] >> 4) & 0xf];
        hex_[2 * i + 1] = hex_table[md[i] & 0xf];
    }
    hex_[2 * i] = '\0';
    hex->assign(hex_);
}

err_t Sha1Hex(const string &in, string *out) {
  SHA_CTX c;
  unsigned char sha1[SHA_DIGEST_LENGTH];

  int ret = SHA1_Init(&c);
  assert(ret == 1);
  ret = SHA1_Update(&c, in.c_str(), in.length());
  assert(ret == 1);
  ret = SHA1_Final(sha1, &c);
  assert(ret == 1);

  Sha1MdToHex(sha1, out);
  return ZISYNC_SUCCESS;
}

const size_t IO_BUF_LEN = 32 * 1024;

err_t FileSha1(const string &path, const string &alia, string *out) {
  OsFile file;
  if (file.Open(path, alia, "rb") != 0) {
    return ZISYNC_ERROR_OS_IO;
  }
  
  SHA_CTX c;
  string buf;
  buf.resize(IO_BUF_LEN);
  size_t size;
  int ret = SHA1_Init(&c);
  assert(ret == 1);
  while((size = file.Read(&buf)) == IO_BUF_LEN) {
    if (zs::IsAborted()) {
      return ZISYNC_ERROR_OS_IO;
    }
    ret = SHA1_Update(&c, buf.c_str(), size);
    assert(ret == 1);
  }

  if (file.eof() != 0) {
    ret = SHA1_Update(&c, buf.c_str(), size);
    assert(ret == 1);
    unsigned char sha1[SHA_DIGEST_LENGTH];
    ret = SHA1_Final(sha1, &c);
    assert(ret == 1);
    Sha1MdToHex(sha1, out);
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_OS_IO;
  }
}

err_t Md5Bin(const string& in, string *out) {
  MD5_CTX c;
  out->resize(MD5_DIGEST_LENGTH);
  unsigned char* md5 = (unsigned char*)(&(*out->begin()));  // NOLINT

  int ret = MD5_Init(&c);
  assert(ret == 1);
  ret = MD5_Update(&c, in.c_str(), in.length());
  assert(ret == 1);
  ret = MD5_Final(md5, &c);
  assert(ret == 1);

  return ZISYNC_SUCCESS;
}

void Md5MdToHex(unsigned char *md, string *hex) {
    int i;
    char hex_[41];
    for (i = 0; i < MD5_DIGEST_LENGTH; i ++) {
        hex_[2 * i] = hex_table[(md[i] >> 4) & 0xf];
        hex_[2 * i + 1] = hex_table[md[i] & 0xf];
    }
    hex_[2 * i] = '\0';
    hex->assign(hex_);
}

err_t Md5Hex(const string &in, string *out) {
  MD5_CTX c;
  unsigned char md5[MD5_DIGEST_LENGTH];

  int ret = MD5_Init(&c);
  assert(ret == 1);
  ret = MD5_Update(&c, in.c_str(), in.length());
  assert(ret == 1);
  ret = MD5_Final(md5, &c);
  assert(ret == 1);

  Md5MdToHex(md5, out);
  return ZISYNC_SUCCESS;
}


}  // namespace zs
