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
#include "zisync/kernel/discover/dht.h"
#include "zisync/kernel/router.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"

using zs::err_t;
using zs::DefaultLogger;
using zs::IZiSyncKernel;
using zs::IDiscoverServer;
using zs::DiscoverServer;
using zs::err_t;
using std::unique_ptr;
using zs::BinToHex;
using zs::HexToBin;
using zs::IContentResolver;
using zs::ContentResolver;
using zs::TableConfig;
using zs::TableDHTPeer;
using zs::GetContentResolver;
using zs::Config;
using zs::IContentResolver;
using zs::ICursor2;

static DefaultLogger logger("./Log");
std::string app_path;

int instance_num;
int32_t discover_port = 8848;
int32_t route_port = 9257;
std::string account;
std::string mynode_id;
std::vector<std::string> sync_uuids;

namespace zs {

static void SHA1String(const std::string& data, std::string* hex_sha1) {
  unsigned char* p;
  unsigned char md[SHA_DIGEST_LENGTH];
  p = reinterpret_cast<unsigned char*>(const_cast<char*>(data.data()));
  SHA1(p, data.length(), md);

  BinToHex('x', reinterpret_cast<const char*>(md),
           SHA_DIGEST_LENGTH, hex_sha1);
}

class TestDiscoverDataSource : public IDiscoverDataSource {
 public:
  TestDiscoverDataSource(int32_t discover_port, std::string& account,
                        std::string& mynode_id,
                        std::vector<std::string>&sync_uuids) :
      discover_port_(discover_port), account_(account),
      sync_uuids_(sync_uuids) {
        std::string mynode_id_hex;
        SHA1String(mynode_id, &mynode_id_hex);
        HexToBin(mynode_id_hex, &mynode_id_);
  }
  virtual ~TestDiscoverDataSource() {}

  virtual err_t Initialize() {
    return ZISYNC_SUCCESS;
  }
  virtual int32_t GetDiscoverPort() {return discover_port_;}
  const std::string GetAccount() {return account_;}
  virtual const std::string GetMyNodeId() {return mynode_id_;}

  virtual err_t GetTrackers(std::vector<std::string>* super_nodes) {
    static const char* dht_super_nodes[] = {
#ifdef ZS_TEST
      "udp://10.88.1.111:9876",
      "udp://10.88.1.176:9876",
#endif
      "udp://sn0.zisync.com:8848",
      "udp://sn1.zisync.com:8848",
    };
    for(size_t i = 0; i < ARRAY_SIZE(dht_super_nodes); i++) {
      super_nodes->push_back(dht_super_nodes[i]);
    }
    return ZISYNC_SUCCESS;
  }

  virtual err_t GetSuperNodes(std::vector<std::string>* super_nodes) {
    static const char* dht_super_nodes[] = {
      "10.88.1.152:9876"
    };
    for(int i = 0; i < 1; i++) {
      super_nodes->push_back(std::string(dht_super_nodes[i]));
    }
  return ZISYNC_SUCCESS;
  }

  err_t GetShareSyncs(std::vector<std::string>* sync_uuids) {
    *sync_uuids = sync_uuids_;
    return ZISYNC_SUCCESS;
  }
  virtual err_t GetInfoHashes(std::vector<std::string>* infohashes){
    string infohash_bin;
    Sha1Bin("ddddddddddddddddddddd", &infohash_bin);
    infohashes->push_back(infohash_bin);
    return ZISYNC_SUCCESS;
  }
  
  virtual err_t StorePort(int32_t new_port) { return ZISYNC_SUCCESS;}
  struct Peers {
    std::string info_hash;
    std::string host;
    int prot, is_lan, is_ipv6;
  };
  virtual void  StorePeers(const std::string infohash, const bool is_ipv6,
                           const std::vector<SearchNode> &peers) {
    for (size_t i = 0; i < peers.size(); i++) {
      Peers peer = {infohash, peers[i].host_, peers[i].port_, peers[i].is_lan_, is_ipv6};
      store_peers_.push_back(peer);
    }
  }

  virtual err_t GetStaticPeers(std::vector<IpPort> *staticaddrs) {
    staticaddrs->insert(staticaddrs->begin(), static_peers_.begin(), static_peers_.end());
    return ZISYNC_SUCCESS;
  }

  virtual err_t StoreStaticPeers(const std::vector<IpPort> &staticaddrs) {
    static_peers_.insert(static_peers_.end(), staticaddrs.begin(), staticaddrs.end());
    return ZISYNC_SUCCESS;
  }

  virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs) {
    size_t i = 0, j = 0;
    for (i = 0; i < staticaddrs.size(); i++) {
      for (j =0; j < static_peers_.size(); j++) {
        if (strcmp(staticaddrs[i].ip_.c_str(), static_peers_[j].ip_.c_str()) == 0 &&
            staticaddrs[i].port_ == static_peers_[j].port_) {
          break;
        }
      }
      if (j == static_peers_.size()) {
        static_peers_.push_back(staticaddrs[i]);
      }
    }
    return ZISYNC_SUCCESS;
  }

  virtual err_t SaveStaticPeers(const std::vector<IpPort> &staticaddrs) {
    static_peers_.clear();
    return StoreStaticPeers(staticaddrs);
  }

  int GetPeer(const std::string& id) {
    int result = 0;
    std::string info_hash;
    SHA1String(id, &info_hash);
    for (int i = 0; i < static_cast<int>(store_peers_.size()); i++) {
      if (store_peers_[i].info_hash == info_hash) result++;
    }
    return result;
  }

 private:
  int32_t discover_port_;
  std::string account_;
  std::string mynode_id_;
  std::vector<std::string> sync_uuids_;
  std::vector<Peers> store_peers_;
  std::vector<IpPort> static_peers_;
};

TEST(test_DiscoverServer) {
  GlobalContextInit();
  err_t zisync_ret = ZISYNC_SUCCESS;
  TestDiscoverDataSource datastore(discover_port, account, mynode_id, sync_uuids);

  IpPort peer1("10.88.1.175", 8848);
  std::vector<IpPort> input;
  // IpPort peer2("10.88.1.232", 8787);
  // IpPort peer3("10.88.3.45", 9887);
  input.push_back(peer1);
  // input.push_back(peer2);
  // input.push_back(peer3);
  datastore.StoreStaticPeers(input);

  DiscoverServer discover_server(&datastore);
  zisync_ret = discover_server.Initialize(route_port);
  assert(zisync_ret == ZISYNC_SUCCESS);

  ILibEventBase* event_base = GetEventBase();
  event_base->RegisterVirtualServer(&discover_server);
  event_base->Startup(); 

  sleep(10);

  int time = 0;
  int result1 = 0;
  while (time <= 1000) {
    sleep(2);
    time += 2;
    if (result1 != instance_num)
      result1 = datastore.GetPeer(account);
    printf("GetPeer account:%s, result:%d\n", account.data(), result1);
  }

  event_base->Shutdown(); 
  event_base->UnregisterVirtualServer(DiscoverServer::GetInstance());

  discover_server.CleanUp();
  GlobalContextFinal();

  printf("CHECK finished.\n");
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
    case 'm':
      mynode_id = optarg;
      break;
    case 's':
      sync_uuids.push_back(optarg);
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
    case 'm':
      mynode_id = optarg_a;
      break;
    case 's':
      sync_uuids = optarg_a;
      break;
    default:
      break;
    }
  }
#endif
    
  int index = optind;
  while (index < argc) {
    sync_uuids.push_back(std::string(argv[index]));
    index++;
  }

  logger.Initialize();
  zs::LogInitialize(&logger);

  logger.error_to_stderr = true;
  UnitTest::RunAllTests();
  zs::LogCleanUp();
  logger.CleanUp();
  return 0;
}

