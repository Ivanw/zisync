/**
 * @file test_icontent.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test case for icontent.cc.
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

#include <cstring>
#include <UnitTest++/UnitTest++.h>  // NOLINT

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/platform/platform.h"


#ifdef _MSC_VER
/*MSVC*/
#endif
#ifdef __GNUC__
/*GCC*/
#endif

using zs::err_t;
using zs::ZmqMsg;
using zs::ZmqContext;
using zs::ZmqSocket;
using zs::ZISYNC_SUCCESS;
using zs::MsgRefreshRequest;
using zs::RefreshRequest;
using zs::RefreshResponse;
using zs::OsThread;
using zs::Mutex;
using zs::Cond;
using zs::IOnTimer;
using zs::OsTimer;
using zs::OsTimeInS;

void test_free(void *data, void *hint) {
  free(data);
}

TEST(test_ZmqMsg) {
  ZmqMsg msg1;
  CHECK_EQUAL(0, static_cast<int>(msg1.size()));

  ZmqMsg msg2(10);
  CHECK_EQUAL(10, static_cast<int>(msg2.size()));
  msg2.Resize(20);
  CHECK_EQUAL(20, static_cast<int>(msg2.size()));

  msg2.SetData(static_cast<const void*>("1234"), 5);
  CHECK_EQUAL(0, strcmp(static_cast<char*>(const_cast<void*>(msg2.data())),
                        "1234"));

  void *data = malloc(5);
  memcpy(data, "1234", 5);
  msg2.SetData(data, 5, test_free);
  CHECK_EQUAL(0, strcmp(static_cast<char*>(const_cast<void*>(msg2.data())),
                        "1234"));
}

class TestOnTimer : public IOnTimer {
 public:
  TestOnTimer(const ZmqContext& context, int check_interval, const char* uri)
    : context_(&context), check_interval_(check_interval), uri_(uri) {
        start_time_ = OsTimeInS();
      }
  virtual void OnTimer() {
    int interval = std::abs(OsTimeInS() - start_time_);
    CHECK_EQUAL(check_interval_, interval);
    // cond->Signal();
    ZmqSocket push_socket(*context_, ZMQ_PUSH);
    push_socket.Connect(uri_.c_str());
    ZmqMsg exit(std::string("exit"));
    exit.SendTo(push_socket, 0);
  }

 private:
  const ZmqContext* context_;
  int start_time_;
  int check_interval_;
  std::string uri_;
};

TEST(test_timer) {
  ZmqContext context;
  ZmqSocket pull_socket1(context, ZMQ_PULL);
  ZmqSocket pull_socket2(context, ZMQ_PULL);

  const char* uri1 = "inproc://push-pull-1";
  const char* uri2 = "inproc://push-pull-2";
  pull_socket1.Bind(uri1);
  pull_socket2.Bind(uri2);
  
  TestOnTimer on_timer(context, 2, uri1);
  OsTimer timer(2000, &on_timer);
  TestOnTimer on_timer2(context, 4, uri2);
  OsTimer timer2(4000, &on_timer2);
  timer.Initialize();
  timer2.Initialize();
  
  ZmqMsg msg;
  msg.RecvFrom(pull_socket1);
  timer.CleanUp();

  msg.RecvFrom(pull_socket2);
  timer2.CleanUp();
}

TEST(test_AesCipher) {
}

