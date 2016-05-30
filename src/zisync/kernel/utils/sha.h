// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_SHA_H_
#define ZISYNC_KERNEL_UTILS_SHA_H_

#include <openssl/sha.h>
#include <openssl/md5.h>
#include <string>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

using std::string;

err_t Md5Bin(const string& in, string *out);
err_t Md5Hex(const string &in, string *out);
err_t Sha1Bin(const string& in, string *out);
err_t Sha1Hex(const string &in, string *out);
err_t FileSha1(const string &path, const string &alias, string *out);
void  Sha1MdToHex(unsigned char *md, string *hex);

}  // namespace zs


#endif  // ZISYNC_KERNEL_UTILS_SHA_H_
