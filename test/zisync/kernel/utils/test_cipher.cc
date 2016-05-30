// Copyright 2014, zisync.com

#include "zisync/kernel/platform/platform.h"

#include <UnitTest++/UnitTest++.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/cipher.h"
#include <string>
#include <vector>
#include <iostream>
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/utils/zmq.h"

using std::string;


TEST(AES) {
  string key = zs::GenAesKey("test");
  zs::AesCipher cipher(key);
  zs::MsgRefreshRequest request;
  request.set_tree_id(3);
  zs::ZmqMsg in(request);
  zs::ZmqMsg out;
  zs::err_t zisync_ret = cipher.Encrypt(&in, &out);
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  zs::ZmqMsg de_out;
  cipher.Decrypt(&out, &de_out);
  CHECK_EQUAL(in.size(), de_out.size());
  bool ret = request.ParseFromArray(de_out.data(), de_out.size());
  CHECK_EQUAL(true, ret);
  CHECK_EQUAL(3, request.tree_id());
}
