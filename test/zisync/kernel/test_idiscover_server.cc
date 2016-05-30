/**
 * @file test_discover_worker.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test cases for DiscoverServer.
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

#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <openssl/sha.h>
#ifndef _MSC_VER
  #include <arpa/inet.h>
#else
  #include <getopt.h>
#endif


#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/endpoint.h"

#include "zisync/kernel/libevent/discover_server.h"
#include "zisync/kernel/libevent/discover_data_source.h"
#include "zisync/kernel/libevent/dht.h"
#include "zisync/kernel/router.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"

using zs::err_t;
using zs::DefaultLogger;
using zs::IDiscoverServer;
using zs::ZmqContext;
using zs::err_t;
using std::unique_ptr;
using zs::BinToHex;
using zs::HexToBin;
using zs::IContentResolver;
using zs::TableConfig;
using zs::TableDHTPeer;
using zs::GetContentResolver;
using zs::Config;
using zs::ICursor2;

static DefaultLogger logger("./Log");
std::string app_path = "discover/case1";

std::string account;

namespace zs {

static void SHA1String(const std::string& data, std::string* hex_sha1) {
  unsigned char* p;
  unsigned char md[SHA_DIGEST_LENGTH];
  p = reinterpret_cast<unsigned char*>(const_cast<char*>(data.data()));
  SHA1(p, data.length(), md);

  BinToHex('x', reinterpret_cast<const char*>(md),
           SHA_DIGEST_LENGTH, hex_sha1);
}

int GetPeerFromDb(std::string& id) {
  IContentResolver* resolver = GetContentResolver();
  assert(resolver);
  std::string id_hex;
  SHA1String(id, &id_hex);
  int count = 0;
  const char* projection[] = { TableDHTPeer::COLUMN_PEER_HOST };
  std::unique_ptr<ICursor2> cursor(
     resolver->Query(TableDHTPeer::URI, projection, ARRAY_SIZE(projection),
                     " %s = '%s' ", TableDHTPeer::COLUMN_INFO_HASH,
                     id_hex.data()));
  while (cursor->MoveToNext()) {
    count++;
  }
  return count;
}

TEST(test_IDiscoverServer) {
  err_t zisync_ret = StartupContentFramework(app_path.data());
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);

  // Config::set_discover_port(discover_port);
  // Config::set_route_port(route_port);
  Config::set_account_name(account);

  ILibEventBase* event_base = GetEventBase();
  event_base->RegisterVirtualServer(DiscoverServer::GetInstance());
  event_base->Startup(); 

  int time = 0, result1 = 0;
  while (time <= 1000) {
    sleep(2);
    time += 2;

    result1 = GetPeerFromDb(account);
    printf("GetPeer account:%s, result:%d\n", account.data(), result1);
  }

  // CleanUp()
  zisync_ret = ShutdownContentFramework();
  CHECK_EQUAL(zs::ZISYNC_SUCCESS, zisync_ret);
}

} // namespace zs

int main(int argc , char** argv) {
  int ch;
#ifndef _MSC_VER
  while ((ch = getopt(argc, argv, "i:p:d:r:a:m:s:")) != -1) {
    switch (ch) {
    case 'a':
      account = optarg;
      break;
    default:
      break;
    }
  }
#else
  while ((ch = getopt_a(argc, argv, "i:p:d:r:a:m:s:")) != -1) {
    switch (ch) {
    case 'a':
      account = optarg_a;
      break;
    default:
      break;
    }
  }
#endif

  logger.Initialize();
  zs::LogInitialize(&logger);
  logger.error_to_stderr = true;
  logger.info_to_stdout = true;
  UnitTest::RunAllTests();
  zs::LogCleanUp();
  logger.CleanUp();
  return 0;
}
