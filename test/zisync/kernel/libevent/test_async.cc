/****************************************************************************
 *       Filename:  test_async.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  03/12/15 16:57:37
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author: Panghai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <iostream>

#include "zisync/kernel/libevent/libevent++.h"

struct SomeFixture {
  SomeFixture() {
    event_base_ = zs::GetEventBaseDb();
    event_base_->Startup();
  }

  ~SomeFixture() {
    event_base_->Shutdown();
  }

  zs::ILibEventBase *event_base_;
};

TEST_FIXTURE(SomeFixture, test_Async) {
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
  event_base_->DispatchAsync(
      [](void *ctx) {std::cout << __LINE__ << std::endl;}, NULL, NULL);
}

int main(int /* argc */, char** /* argv */) {
  UnitTest::RunAllTests();

  return 0;
}
