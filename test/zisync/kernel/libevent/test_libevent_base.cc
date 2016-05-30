/**
 * @file test_libevent_base.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test case for libevnet_base.cc.
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

#include <stdio.h>
#include <UnitTest++/UnitTest++.h>  // NOLINT

#include "zisync/kernel/libevent/libevent_base.h"

namespace zs {

LibEventBase* s_base = NULL;

struct SomeFixture {
  SomeFixture() {
    s_base = new LibEventBase();
  }

  ~SomeFixture() {
    if (s_base) {
      delete s_base;
      s_base = NULL;
    }
  }
};

TEST(test_LibeventBaseStartAndStop) {
  err_t rc = ZISYNC_SUCCESS;
  assert(s_base == NULL);
  
  s_base = new LibEventBase();
  rc = s_base->Startup();
  CHECK(rc == ZISYNC_SUCCESS);
  rc = s_base->Shutdown();
  CHECK(rc == ZISYNC_SUCCESS);

  if (s_base) {
    delete s_base;
    s_base = NULL;
  }
  assert(s_base == NULL);
}

class LibEventBaseTester {
 public:
  static size_t virtual_server_size(LibEventBase* base) {
    return base->virtual_servers_.size();
  }
};

class VirtualServerTest : public ILibEventVirtualServer {
 public:
  VirtualServerTest() : state_(0) {  }
  virtual ~VirtualServerTest() {  }
  
  virtual err_t Startup(ILibEventBase* base) {
    event_base_ = base;
    state_.set_value(1);
    event_started_->Signal();
    return ZISYNC_SUCCESS;
  }
  
  virtual err_t Shutdown(ILibEventBase* base) {
    state_.set_value(2);
    event_base_ = NULL;
    return ZISYNC_SUCCESS;
  }

  err_t Initialize() {
    event_started_.reset(new OsEvent);
    if (event_started_->Initialize(false) != 0) {
      return ZISYNC_ERROR_OS_EVENT;
    }
    return ZISYNC_SUCCESS;
  }

  err_t CleanUp() {  return ZISYNC_SUCCESS; }

  bool Wait() {
    return event_started_->Wait() == 0;
  }

  int32_t state() {
    return state_.value();
  }
  
 private:
  ILibEventBase* event_base_;

  AtomicInt32 state_;
  std::unique_ptr<OsEvent> event_started_;
};

TEST_FIXTURE(SomeFixture, test_LibeventBaseRegisterVirtualServer) {
  err_t rc = ZISYNC_SUCCESS;
  auto vs1 = new VirtualServerTest;
  auto vs2 = new VirtualServerTest;

  CHECK(vs1->Initialize() == ZISYNC_SUCCESS);
  CHECK(vs2->Initialize() == ZISYNC_SUCCESS);
  
  s_base->RegisterVirtualServer(vs1);
  s_base->RegisterVirtualServer(vs2);
  CHECK(LibEventBaseTester::virtual_server_size(s_base) == 2);
  
  rc = s_base->Startup();
  CHECK(rc == ZISYNC_SUCCESS);

  CHECK(vs1->Wait() == true);
  CHECK(vs1->state() == 1);

  CHECK(vs2->Wait() == true);
  CHECK(vs2->state() == 1);

  rc = s_base->Shutdown();
  CHECK(rc == ZISYNC_SUCCESS);
  CHECK(vs1->state() == 2);
  CHECK(vs2->state() == 2);

  s_base->UnregisterVirtualServer(vs1);
  s_base->UnregisterVirtualServer(vs2);
  CHECK(LibEventBaseTester::virtual_server_size(s_base) == 0);

  CHECK(vs1->CleanUp() == ZISYNC_SUCCESS);
  CHECK(vs2->CleanUp() == ZISYNC_SUCCESS);
}

}  // namespace zs


