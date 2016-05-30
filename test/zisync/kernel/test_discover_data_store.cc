/**
 * @file test_discover_worker.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test cases for DiscoverWorker.
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
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"

using zs::err_t;
using zs::DefaultLogger;
using zs::IZiSyncKernel;
using zs::err_t;
using std::unique_ptr;
using zs::BinToHex;
using zs::HexToBin;
using zs::IContentResolver;
using zs::TableConfig;
using zs::TableDHTPeer;
using zs::GetContentResolver;
using zs::Config;
using zs::IContentResolver;

static unique_ptr<IZiSyncKernel> kernel(zs::GetZiSyncKernel("actual"));

static DefaultLogger logger("./Log");
std::string app_path = "/home/luna/workdir/ZiSyncKernel/test/zisync/kernel";
std::string backup_path = "/home/luna/workdir/ZiSyncKernel/test/zisync/kernel/backup";

namespace zs {

static void PutIpPort(std::vector<IpPort> peers) {
  IContentResolver *resolver = GetContentResolver();
  OperationList op_list;

  for (size_t i = 0; i < peers.size(); i++) {
    ContentOperation *cp = op_list.NewInsert(TableStaticPeer::URI, AOC_REPLACE);
    ContentValues *cv = cp->GetContentValues();

    cv->Put(TableStaticPeer::COLUMN_IP, peers[i].ip_);
    cv->Put(TableStaticPeer::COLUMN_PORT, peers[i].port_);
  }

  resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
}

/*
static void SHA1String(const std::string& data, std::string* hex_sha1) {
  unsigned char* p;
  unsigned char md[SHA_DIGEST_LENGTH];
  p = reinterpret_cast<unsigned char*>(const_cast<char*>(data.data()));
  SHA1(p, data.length(), md);
  BinToHex('x', reinterpret_cast<const char*>(md),
           SHA_DIGEST_LENGTH, hex_sha1);
}

static bool GetPeerInfo(std::string& info_hash, std::string* host,
                       int* port, int* is_lan, int* is_ipv6) {
  IContentResolver* resolver = GetContentResolver();
  std::string str_ret;
  bool ret = resolver->QueryString(
      TableDHTPeer::URI, TableDHTPeer::COLUMN_PEER_HOST,
      &str_ret, " %s = '%s' ", TableDHTPeer::COLUMN_INFO_HASH,
      info_hash.data());
  if (ret) {
    *host = str_ret;
    ret = resolver->QueryString(
        TableDHTPeer::URI, TableDHTPeer::COLUMN_PEER_PORT,
        &str_ret, " %s = '%s' ", TableDHTPeer::COLUMN_INFO_HASH,
        info_hash.data());
  }
  if (ret) {
    *port = atoi(str_ret.data());
    ret = resolver->QueryString(
        TableDHTPeer::URI, TableDHTPeer::COLUMN_PEER_IS_LAN,
        &str_ret, " %s = '%s' ", TableDHTPeer::COLUMN_INFO_HASH,
        info_hash.data());
  }
  if (ret) {
    *is_lan = atoi(str_ret.data());
    ret = resolver->QueryString(
        TableDHTPeer::URI, TableDHTPeer::COLUMN_PEER_IS_IPV6,
        &str_ret, " %s = '%s' ", TableDHTPeer::COLUMN_INFO_HASH,
        info_hash.data());
  }
  if (ret) {
    *is_ipv6 = atoi(str_ret.data());
  }
  return ret;
}

TEST(test_GetPort) {
  DiscoverDataStore data_store;
  int32_t discover_port = data_store.GetPort();
  CHECK(discover_port > 0 && discover_port < 65536);

  int32_t old_port = Config::discover_port();
  Config::set_discover_port(6738);
  discover_port = data_store.GetPort();
  CHECK(discover_port == 6738);
  Config::set_discover_port(old_port);
}

TEST(test_GetAccount) {
  DiscoverDataStore data_store;
  std::string account = data_store.GetAccount();
  CHECK(!account.empty());

  std::string old_account = data_store.GetAccount();
  Config::set_account_name("lunabox1001");
  account = data_store.GetAccount();
  CHECK(account == "lunabox1001");
  Config::set_account_name(old_account);
}

TEST(test_GetMyNodeId) {
  std::string myid, myid_gen, myid_bin;
  SHA1String(Config::device_uuid(), &myid_gen);
  HexToBin(myid_gen, &myid_bin);
  DiscoverDataStore data_store;
  myid = data_store.GetMyNodeId();
  CHECK(myid == myid_bin);

  // again for get MyNode from database
  myid.clear();
  myid = data_store.GetMyNodeId();
  CHECK(myid == myid_bin);
}

TEST(test_GetSuperNodes) {
  err_t err;
  std::vector<std::string> super_nodes;
  DiscoverDataStore data_store;
  err = data_store.GetSuperNodes(&super_nodes);
  CHECK(err == ZISYNC_SUCCESS);
  CHECK(super_nodes.size() != 0);
}

TEST(test_GetShareSyncs) {
  err_t err;
  std::vector<std::string> sync_uuids;
  DiscoverDataStore data_store;
  err = data_store.GetSuperNodes(&sync_uuids);
  CHECK(err == ZISYNC_SUCCESS);
  CHECK(sync_uuids.size() != 0);
}

TEST(test_StorePort) {
  err_t err;
  int32_t old_port = Config::discover_port();
  DiscoverDataStore data_store;
  err = data_store.StorePort(5648);
  CHECK(err == ZISYNC_SUCCESS);
  CHECK(Config::discover_port() == 5648);
  data_store.StorePort(old_port);
}

TEST(test_StorePeer) {
  std::string info_hash;
  const std::string host("10.88.1.173");
  int port = 7647, is_lan = 0, is_ipv6 = 0;
  SHA1String(Config::device_uuid(), &info_hash);
  DiscoverDataStore data_store;
  data_store.StorePeer(info_hash, host, port, is_lan, is_ipv6);
  std::string host_get;
  int port_get, is_lan_get, is_ipv6_get;
  GetPeerInfo(info_hash, &host_get, &port_get, &is_lan_get, &is_ipv6_get);
  CHECK(host == host_get);
  CHECK(port == port_get);
  CHECK(is_lan == is_lan_get);
  CHECK(is_ipv6 == is_ipv6_get);
  IContentResolver* resolver = GetContentResolver();
  resolver->Delete(TableDHTPeer::URI, " %s = '%s' ",
                   TableDHTPeer::COLUMN_PEER_HOST, host.data());
}

TEST(RefreshSearchCache) {
  std::string account, host;
  int port, is_lan, is_ipv6;
  DiscoverDataStore data_store;

  std::string account1 = "1123455";
  std::string account1_hex, account1_bin;
  SHA1String(account1, &account1_hex);
  HexToBin(account1_hex, &account1_bin);
  struct sockaddr_in sa1;
  sa1.sin_addr.s_addr = inet_addr("10.88.1.12");
  sa1.sin_port = htons(8848);
  std::string host1 = reinterpret_cast<char*>(&sa1.sin_addr);
  host1 += reinterpret_cast<char*>(&sa1.sin_port);
  data_store.StoreData(host1, account1_bin,
                       data_store.FindSearchResult(account1_hex), 1);
  if (!GetPeerInfo(account1_hex, &host, &port, &is_lan, &is_ipv6)) {
    ZSLOG_INFO("GetPeerInfo failed.");
  }
  CHECK(host == "10.88.1.12");
  CHECK(port == 8848);
  CHECK(is_lan == 1);
  CHECK(is_ipv6 == 0);

  std::string account2 = "1123466";
  std::string account2_hex, account2_bin;
  SHA1String(account2, &account2_hex);
  HexToBin(account2_hex, &account2_bin);
  struct sockaddr_in sa2;
  sa2.sin_addr.s_addr = inet_addr("10.88.1.13");
  sa2.sin_port = htons(8848);
  std::string host2 = reinterpret_cast<char*>(&sa2.sin_addr);
  host2 += reinterpret_cast<char*>(&sa2.sin_port);
  data_store.StoreData(host2, account2_bin,
                       data_store.FindSearchResult(account2_hex), 1);
  if (!GetPeerInfo(account2_hex, &host, &port, &is_lan, &is_ipv6)) {
    ZSLOG_INFO("GetPeerInfo failed.");
  }
  CHECK(host == "10.88.1.13");
  CHECK(port == 8848);
  CHECK(is_lan == 1);
  CHECK(is_ipv6 == 0);

  sleep(7);
  
  std::string account3 = "1123466";
  std::string account3_hex, account3_bin;
  SHA1String(account3, &account3_hex);
  HexToBin(account3_hex, &account3_bin);
  struct sockaddr_in sa3;
  sa3.sin_addr.s_addr = inet_addr("10.88.1.14");
  sa3.sin_port = htons(8848);
  std::string host3 = reinterpret_cast<char*>(&sa3.sin_addr);
  host3 += reinterpret_cast<char*>(&sa3.sin_port);
  data_store.StoreData(host3, account3_bin,
                       data_store.FindSearchResult(account3_hex), 1);
  if (!GetPeerInfo(account3_hex, &host, &port, &is_lan, &is_ipv6)) {
    ZSLOG_INFO("GetPeerInfo failed.");
  }

  CHECK(host == "10.88.1.13");
  CHECK(port == 8848);
  CHECK(is_lan == 1);
  CHECK(is_ipv6 == 0);

  data_store.RefreshSearchCache();

  CHECK(GetPeerInfo(account1_hex, &host, &port, &is_lan, &is_ipv6) == true);
  CHECK(GetPeerInfo(account2_hex, &host, &port, &is_lan, &is_ipv6) == true);
  CHECK(GetPeerInfo(account3_hex, &host, &port, &is_lan, &is_ipv6) == true);
}
*/
TEST(GetStaticPeers) {
  DiscoverDataSource data;
  std::vector<IpPort> input, output;

  IpPort peer1("10.88.1.123", 7868);
  IpPort peer2("10.88.1.232", 8787);
  IpPort peer3("10.88.3.45", 9887);
  input.push_back(peer1);
  input.push_back(peer2);
  input.push_back(peer3);
  PutIpPort(input); 

  data.GetStaticPeers(&output);
  bool ispeer1 = false, ispeer2 = false, ispeer3 = false;   
  for (size_t i = 0; output.size(); i++) {
    if (strcmp(output[i].ip_.c_str(), peer1.ip_.c_str()) == 0 && output[i].port_ == peer1.port_) {
      ispeer1 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer2.ip_.c_str()) == 0 && output[i].port_ == peer2.port_) {
      ispeer2 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer3.ip_.c_str()) == 0 && output[i].port_ == peer3.port_) {
      ispeer3 = true;
    }
  }

  CHECK(ispeer1);
  CHECK(ispeer2);
  CHECK(ispeer3);
}

TEST(StoreStaticPeers) {
  DiscoverDataSource data;
  std::vector<IpPort> input, output;

  IpPort peer1("10.88.1.123", 7868);
  IpPort peer2("10.88.1.232", 8787);
  IpPort peer3("10.88.3.45", 9887);
  input.push_back(peer1);
  input.push_back(peer2);
  input.push_back(peer3);
  
  data.StoreStaticPeers(input);
  sleep(1);
  data.GetStaticPeers(&output);

  bool ispeer1 = false, ispeer2 = false, ispeer3 = false;   
  for (size_t i = 0; output.size(); i++) {
    if (strcmp(output[i].ip_.c_str(), peer1.ip_.c_str()) == 0 && output[i].port_ == peer1.port_) {
      ispeer1 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer2.ip_.c_str()) == 0 && output[i].port_ == peer2.port_) {
      ispeer2 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer3.ip_.c_str()) == 0 && output[i].port_ == peer3.port_) {
      ispeer3 = true;
    }
  }

  CHECK(ispeer1);
  CHECK(ispeer2);
  CHECK(ispeer3);
}

TEST(DeleteStaticPeers) {
  DiscoverDataSource data;
  std::vector<IpPort> input, output;

  IpPort peer1("10.88.1.123", 7868);
  IpPort peer2("10.88.1.232", 8787);
  IpPort peer3("10.88.3.45", 9887);
  input.push_back(peer1);
  input.push_back(peer2);
  input.push_back(peer3);
  
  data.StoreStaticPeers(input);
  sleep(1);
  data.DeleteStaticPeers(input);
  sleep(1);
  data.GetStaticPeers(&output);

  bool ispeer1 = false, ispeer2 = false, ispeer3 = false;   
  for (size_t i = 0; output.size(); i++) {
    if (strcmp(output[i].ip_.c_str(), peer1.ip_.c_str()) == 0 &&
        output[i].port_ == peer1.port_) {
      ispeer1 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer2.ip_.c_str()) == 0 &&
        output[i].port_ == peer2.port_) {
      ispeer2 = true;
    }
    if (strcmp(output[i].ip_.c_str(), peer3.ip_.c_str()) == 0 &&
        output[i].port_ == peer3.port_) {
      ispeer3 = true;
    }
  }

  CHECK(ispeer1 == false);
  CHECK(ispeer2 == false);
  CHECK(ispeer3 == false);
}

  // virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs);

} // namespace zs

// #ifndef _MSC_VER

int main(int argc , char** argv) {
  err_t err;
  logger.Initialize();
  zs::LogInitialize(&logger);
  logger.error_to_stderr = true;
  logger.info_to_stdout = true;

  err = kernel->Initialize(
      app_path.c_str(), "test_kernel", "test", backup_path.c_str());

  err = kernel->Initialize(app_path.data(), app_path.data(), "./");
  assert(err == zs::ZISYNC_SUCCESS);
  UnitTest::RunAllTests();
  zs::LogCleanUp();
  logger.CleanUp();
  return 0;
}

// #endif
