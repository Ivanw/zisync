/****************************************************************************
 *       Filename:  test_libevent.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/29/15 16:16:21
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  PangHai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#include <UnitTest++/UnitTest++.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <assert.h>
#include <string.h>
#include <memory>

#include "zisync/kernel/libevent/http_request.h"
#include "zisync/kernel/zslog.h"
#include "zisync_kernel.h"

using zs::HttpRequestHead;

struct bufferevent *pair[2];
struct event_base *event_base = NULL;
HttpRequestHead http_request_head;

zs::err_t OnRead(const char *test_http_header) {
  int ret = bufferevent_write(pair[0], test_http_header, strlen(test_http_header));
  assert(ret == 0);
  ret = bufferevent_flush(pair[0], EV_WRITE, BEV_FLUSH);
  assert(ret >= 0);

  return http_request_head.OnRead(pair[1]);
}

TEST(test_HttpRequestHead) {
  event_base = event_base_new();
  assert(event_base != NULL);

  int ret = bufferevent_pair_new(event_base, 0, pair);
  assert(ret == 0);

  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("PUT tar"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead(" Http/1.1"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\n"));

  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("ZiSync-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Remote-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Tree-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Uuid"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead(":"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("1234567890"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("12345678901234567890123456"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\n"));

  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("ZiSync-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Local-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Tree-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Uuid"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead(":"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("1234567890"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("12345678901234567890123456"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\n"));

  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("ZiSync-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Total-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Size-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead(":"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("123"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\n"));

  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("ZiSync-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Total-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("Files-"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead(":"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("123"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\n"));


  CHECK_EQUAL(zs::ZISYNC_ERROR_AGAIN, OnRead("\r"));
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, OnRead("\n"));

  CHECK(http_request_head.method() == "PUT");
  CHECK(http_request_head.format() == "tar");
  CHECK(http_request_head.remote_tree_uuid() == "123456789012345678901234567890123456");
  CHECK(http_request_head.local_tree_uuid() == "123456789012345678901234567890123456");
  CHECK(http_request_head.total_bytes() == 123);
  CHECK(http_request_head.total_files() == 123);

  bufferevent_free(pair[0]);
  bufferevent_free(pair[1]);
  event_base_free(event_base);
}

static zs::DefaultLogger logger("./Log");

int main(int /* argc */, char** /* argv */) {
  logger.error_to_stderr = false;
  logger.info_to_stdout = false;
  logger.warning_to_stdout = false;
  logger.Initialize();
  LogInitialize(&logger);

  UnitTest::RunAllTests();
  return 0;
}
