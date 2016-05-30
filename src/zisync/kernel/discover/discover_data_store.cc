/**
 * @file discover_data_store.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief discover data store implments.
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
#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <iphlpapi.h>
#include <time.h>
#pragma comment(lib, "IPHLPAPI.lib")
#endif

#include <stdlib.h>
#include <memory>
#include <openssl/sha.h>

#include "discover_data_store.h"

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/utils/context.h"

namespace zs {

DiscoverDataStore::DiscoverDataStore():
    inner_push(NULL) {}
DiscoverDataStore::~DiscoverDataStore() { 
  if (inner_push != NULL) {
    delete inner_push;
  }
}

err_t DiscoverDataStore::Initialize() {
  assert(inner_push == NULL);
  inner_push = new ZmqSocket(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = inner_push->Connect(zs::router_inner_pull_fronter_uri); 
  assert(zisync_ret == ZISYNC_SUCCESS);

  // clear all in DHTPeer
  IContentResolver *resolver = GetContentResolver();
  resolver->Delete(TableDHTPeer::URI, NULL);

  return ZISYNC_SUCCESS;
}

int32_t DiscoverDataStore::GetPort() {
  return Config::discover_port();
}

const std::string DiscoverDataStore::GetMyNodeId() {
  IContentResolver* resolver = GetContentResolver();
  
  std::string myid;

  bool ret = resolver->QueryString(
      TableConfig::URI, TableConfig::COLUMN_VALUE,
      &myid, " %s = 'dhtid' ", TableConfig::COLUMN_NAME);
  if (ret == false) {
    Sha1Hex(Config::device_uuid(), &myid);

    ContentValues cv(2);
    cv.Put(TableConfig::COLUMN_NAME, "dhtid");
    cv.Put(TableConfig::COLUMN_VALUE, myid);
 
    if (resolver->Insert(TableConfig::URI, &cv, AOC_IGNORE) < 0) {
      ZSLOG_ERROR("insert myid to TableConfig fialed.");
    }
  }

  std::string binary_id;
  HexToBin(myid, &binary_id);
  return binary_id;
}

static bool GetSupernodeFromTracker(std::vector<std::string>* supernodes) {
  std::vector<sockaddr_in> host_ipv4;
  std::vector<sockaddr_in6> host_ipv6;
  if (ListIpAddresses(&host_ipv4, &host_ipv6) != 0) {
    ZSLOG_WARNING("Failed to ListIpAddress");
  }

  std::string hostaddrs;
  StringAppendFormat(&hostaddrs, "[");
  for (size_t i = 0; i < host_ipv4.size(); i++) {
    if (i != 0) {
      StringAppendFormat(&hostaddrs, ", ");
    }
    StringAppendFormat(&hostaddrs, "{\"uri\":\"udp://%s:%d\"}",
                       inet_ntoa(host_ipv4[i].sin_addr),
                       Config::discover_port());
  }
  hostaddrs.append("]");
  
  char host[100];
  sscanf(kTrackerUri, "http://%100[^:]:%*d", host);

  std::string buffer;
  StringFormat(&buffer, "POST /supernode HTTP/1.1\r\n" "Host: %s\r\n"
               "Content-Type: text/json\r\n" "Content-Length: %d\r\n" "\r\n" "%s" ,
               host, static_cast<int>(hostaddrs.size()), hostaddrs.data());
  
  OsTcpSocket client_socket(kTrackerUri);
  if (client_socket.Connect() == -1) {
    ZSLOG_ERROR("OsTcpSocket Connect failed: %s", client_socket.uri().data());
    return false;
  }
  if (client_socket.Send(buffer, 0) < 0) {
    ZSLOG_ERROR("OsTcpSocket Send failed!");
    return false;
  }
  client_socket.Shutdown("w");

  fdbuf fd_buf(&client_socket);
  std::istream in(&fd_buf);

  int code = -1;
  std::string line;
  std::getline(in, line, '\n');
  if (!line.empty()) {
    int ret = sscanf(line.c_str(), "HTTP/%*s %d", &code);
    assert(ret > 0);
  }
  
  if (code != 200) {
    ZSLOG_ERROR("get supernode form tracker receive status not OK.");
    return false;
  }

  // skip headers
  while (!in.fail()) {
    std::getline(in, line, '\n');
    if (line == "\r") {
      break;
    }
  }
  
  while (!in.fail()) {
    getline(in, line, '\n');
    if (!line.empty() && line.at(line.size() - 1) == '\r') {
      line.erase(line.size() - 1);
    }
    std::size_t pos = line.find("udp://");
    if (pos == 0) {
      supernodes->push_back(line);
    } else {
      assert(pos == std::string::npos);
    }
  }
  
  return true;
}

err_t DiscoverDataStore::GetSuperNodes(std::vector<std::string>* super_nodes) {
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
  GetSupernodeFromTracker(super_nodes);
  return ZISYNC_SUCCESS;
}

err_t DiscoverDataStore::GetInfoHashes(std::vector<std::string>* infohashes) {
  std::string infohash_bin;
  Sha1Bin(Config::account_name(), &infohash_bin);
  infohashes->push_back(infohash_bin);

  IContentResolver* resolver = GetContentResolver();
  assert(resolver);
  const char* projection[] = { TableSync::COLUMN_UUID };
  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableSync::URI, projection, ARRAY_SIZE(projection), 
          " %s = %d AND %s = %d", 
          TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED, 
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));

  while (cursor->MoveToNext()) {
    Sha1Bin(cursor->GetString(0), &infohash_bin);
    infohashes->push_back(infohash_bin);
  }

  return ZISYNC_SUCCESS;
}

err_t DiscoverDataStore::StorePort(int32_t new_port) {
  assert(new_port > 0 && new_port < 65536);
  Config::set_discover_port(new_port);
  return ZISYNC_SUCCESS;
}

void  DiscoverDataStore::StorePeer(const std::string& info_hash,
                                   const std::string& host,
                                   int port, bool is_lan, bool is_ipv6) {
  if (!IsValidPort(port)) {
    ZSLOG_WARNING("Recv Invalid port(%d)", port);
    return;
  }
  IContentResolver* resolver = GetContentResolver();

  ContentValues cv(5);
  cv.Put(TableDHTPeer::COLUMN_INFO_HASH, info_hash);
  cv.Put(TableDHTPeer::COLUMN_PEER_HOST, host);
  cv.Put(TableDHTPeer::COLUMN_PEER_PORT, port);
  cv.Put(TableDHTPeer::COLUMN_PEER_IS_LAN, is_lan);
  cv.Put(TableDHTPeer::COLUMN_PEER_IS_IPV6, is_ipv6);

  if (resolver->Update(TableDHTPeer::URI, &cv, "%s = '%s' AND %s = %d",
                       TableDHTPeer::COLUMN_INFO_HASH, info_hash.data(),
                       TableDHTPeer::COLUMN_PEER_HOST, port) <= 0) {
    if (resolver->Insert(TableDHTPeer::URI, &cv, AOC_IGNORE) < 0) {
      ZSLOG_ERROR("Failed to save Peer information to database");
      return;
    }

    // only insert need to DeviceInfo
    string user_sha1;
    Sha1Hex(GetAccount(), &user_sha1);
    if (user_sha1 == info_hash) {
      IssueDeviceInfo(host, port, NULL, is_ipv6);
      // IssuePushDeviceInfo(host, port, NULL, is_ipv6);
    } else {
      std::string uuid_hex;
      std::vector<std::string> sync_uuids;
      err_t zisync_t = ZISYNC_SUCCESS;
      zisync_t = GetShareSyncs(&sync_uuids);
      if (zisync_t != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("GetShareSynce failed.");
        return;
      }
      size_t size = sync_uuids.size();
      for (size_t i = 0; i < size; ++i) {
        Sha1Hex(sync_uuids[i], &uuid_hex);
        if (info_hash == uuid_hex) {
          IssueDeviceInfo(host, port, sync_uuids[i].c_str(), is_ipv6);
          // IssuePushDeviceInfo(host, port, sync_uuids[i].c_str(), is_ipv6);
          return;
        }
      }
    }
  }
}

const std::string DiscoverDataStore::GetAccount() {
  return Config::account_name();
}

err_t DiscoverDataStore::GetShareSyncs(std::vector<std::string>* sync_uuids) {
  IContentResolver* resolver = GetContentResolver();
  assert(resolver);
  const char* projection[] = { TableSync::COLUMN_UUID };
  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableSync::URI, projection, ARRAY_SIZE(projection), 
          " %s = %d AND %s = %d", 
          TableSync::COLUMN_TYPE, TableSync::TYPE_SHARED, 
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  while (cursor->MoveToNext()) {
    sync_uuids->push_back(cursor->GetString(0));
  }
  return ZISYNC_SUCCESS;
}


void DiscoverDataStore::IssueDeviceInfo(
    const std::string& host, int port, const char *sync_uuid, 
    bool is_ipv6) {
  if (inner_push == NULL) {
    return;
  }
  ZSLOG_INFO("Send IssueDeviceInfoRequest(%s:%d)", host.c_str(), port);
  IssueDeviceInfoRequest request;
  MsgUri *uri = request.mutable_request()->mutable_uri();
  uri->set_host(host);
  uri->set_port(port);
  uri->set_is_ipv6(is_ipv6);
  if (sync_uuid != NULL) {
    request.mutable_request()->set_sync_uuid(sync_uuid);
  }
  err_t zisync_ret = request.SendTo(*inner_push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

}  // namespace zs
