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

#include "zisync/kernel/libevent/discover_data_source.h"

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/transfer/fdbuf.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/permission.h"

namespace zs {

StaticPeerObserver::StaticPeerObserver() {
  if (peers_mutex_.Initialize() != 0) {
    ZSLOG_ERROR("failed to initialize dispatch_mutex");
    assert(false);
  }
  // clear all in DHTPeer
  // IContentResolver *resolver = GetContentResolver();
  // resolver->Delete(TableDHTPeer::URI, NULL);

  // fill infohash cache by call OnHandleChange()
  OnHandleChange(NULL);

  GetContentResolver()->RegisterContentObserver(
      TableStaticPeer::URI, false, this);
}

StaticPeerObserver::~StaticPeerObserver() {
  GetContentResolver()->UnregisterContentObserver(
      TableStaticPeer::URI, this);
  peers_mutex_.CleanUp();
}

void* StaticPeerObserver::OnQueryChange() {
  return NULL;
}

void StaticPeerObserver::OnHandleChange(void* lpChanges) {
  std::vector<IpPort> temp;
  IpPort peer; 
  IContentResolver* resolver = GetContentResolver();
  assert(resolver);
  const char* projection[] = { TableStaticPeer::COLUMN_IP, TableStaticPeer::COLUMN_PORT };
  std::unique_ptr<ICursor2> cursor(resolver->Query(
          TableStaticPeer::URI, projection, ARRAY_SIZE(projection), NULL));

  while (cursor->MoveToNext()) {
    peer.ip_ = cursor->GetString(0);
    peer.port_ = cursor->GetInt32(1);
    temp.push_back(peer);
  }

  {
    MutexAuto mutex_auto(&peers_mutex_);
    peers_.swap(temp);
  }
}

err_t StaticPeerObserver::GetStaticPeers(std::vector<IpPort> *staticaddrs) {
  if (staticaddrs == NULL) {
    return ZISYNC_ERROR_GENERAL;
  }
  staticaddrs->clear();
  staticaddrs->insert(staticaddrs->begin(), peers_.begin(), peers_.end());
  return ZISYNC_SUCCESS;
}

DiscoverDataSource::DiscoverDataSource() {
  if (infohashes_mutex_.Initialize() != 0) {
    ZSLOG_ERROR("failed to initialize dispatch_mutex");
    assert(false);
  }

  // clear all in DHTPeer
  IContentResolver *resolver = GetContentResolver();
  resolver->Delete(TableDHTPeer::URI, NULL);

  // fill infohash cache by call OnHandleChange()
  OnHandleChange(NULL);

  GetContentResolver()->RegisterContentObserver(
      TableSync::URI, false, this);
  static_peer_source_ = new StaticPeerObserver;
}

DiscoverDataSource::~DiscoverDataSource() { 
  if (static_peer_source_) {
    delete static_peer_source_;
  }

  GetContentResolver()->UnregisterContentObserver(
      TableSync::URI, this);
  infohashes_mutex_.CleanUp();
}

void* DiscoverDataSource::OnQueryChange() {
  return NULL;
}

void DiscoverDataSource::OnHandleChange(void* lpChanges) {
  std::vector<std::string> temp; 
  std::string infohash_bin;

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
    temp.push_back(infohash_bin);
  }

  {
    MutexAuto mutex_auto(&infohashes_mutex_);
    infohashes_.swap(temp);
  }
}

int32_t DiscoverDataSource::GetDiscoverPort() {
  return Config::discover_port();
}

int32_t DiscoverDataSource::GetRoutePort() {
  return 0; 
}

const std::string DiscoverDataSource::GetMyNodeId() {
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

err_t DiscoverDataSource::GetTrackers(std::vector<std::string>* super_nodes) {
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
  //GetSupernodeFromTracker(super_nodes);
  return ZISYNC_SUCCESS;
}

err_t DiscoverDataSource::GetInfoHashes(std::vector<std::string>* infohashes) {
  std::string infohash_bin;
  Sha1Bin(Config::account_name(), &infohash_bin);
  infohashes->push_back(infohash_bin);

  {
    MutexAuto mutex_auto(&infohashes_mutex_);
    infohashes->insert(infohashes->end(), infohashes_.begin(), infohashes_.end());
  }

  return ZISYNC_SUCCESS;
}

err_t DiscoverDataSource::StorePort(int32_t new_port) {
  assert(new_port > 0 && new_port < 65536);
  Config::set_discover_port(new_port);
  return ZISYNC_SUCCESS;
}

void DiscoverDataSource::StorePeers(const std::string infohash, const bool is_ipv6,
                                    const std::vector<SearchNode> &peers) {
  IContentResolver *resolver = GetContentResolver();
  OperationList op_list;

  for (size_t i = 0; i < peers.size(); i++) {
    if (!IsValidPort(peers[i].port_)) {
      ZSLOG_WARNING("Recv Invalid port(%d)", peers[i].port_);
      continue;
    }

    ContentOperation *cp = op_list.NewInsert(TableDHTPeer::URI, AOC_REPLACE);
    ContentValues *cv = cp->GetContentValues();

    cv->Put(TableDHTPeer::COLUMN_INFO_HASH, infohash);
    cv->Put(TableDHTPeer::COLUMN_PEER_HOST, peers[i].host_);
    cv->Put(TableDHTPeer::COLUMN_PEER_PORT, peers[i].port_);
    cv->Put(TableDHTPeer::COLUMN_PEER_IS_LAN, peers[i].is_lan_);
    cv->Put(TableDHTPeer::COLUMN_PEER_IS_IPV6, is_ipv6);
  }

  resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
}

const std::string DiscoverDataSource::GetAccount() {
  return Config::account_name();
}

err_t DiscoverDataSource::GetShareSyncs(std::vector<std::string>* sync_uuids) {
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

err_t DiscoverDataSource::GetStaticPeers(std::vector<IpPort> *staticaddrs) {
  assert(static_peer_source_);
  return static_peer_source_->GetStaticPeers(staticaddrs);
}

err_t DiscoverDataSource::StoreStaticPeers(
    const std::vector<IpPort> &staticaddrs) {
  int32_t count = static_peer_source_->peers_size() + staticaddrs.size() - 1;
  if (!GetPermission()->Verify(USER_PERMISSION_CNT_STATICIP, &count)) {
    ZSLOG_INFO("You have no permission to point ip.");
    return ZISYNC_ERROR_PERMISSION_DENY;
  }

  IContentResolver *resolver = GetContentResolver();
  assert(resolver);
  OperationList op_list;

  for (size_t i = 0; i < staticaddrs.size(); i++) {
    if (!IsValidPort(staticaddrs[i].port_)) {
      ZSLOG_WARNING("Recv Invalid port(%d)", staticaddrs[i].port_);
      continue;
    }

    ContentOperation *cp = op_list.NewInsert(TableStaticPeer::URI, AOC_REPLACE);
    ContentValues *cv = cp->GetContentValues();

    cv->Put(TableStaticPeer::COLUMN_IP, staticaddrs[i].ip_);
    cv->Put(TableStaticPeer::COLUMN_PORT, staticaddrs[i].port_);
  }

  resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
  return ZISYNC_SUCCESS;
}

err_t DiscoverDataSource::DeleteStaticPeers(const std::vector<IpPort> &staticaddrs) {
  IContentResolver *resolver = GetContentResolver();
  assert(resolver);
  OperationList op_list;

  for (size_t i = 0; i < staticaddrs.size(); i++) {
    if (!IsValidPort(staticaddrs[i].port_)) {
      ZSLOG_WARNING("Recv Invalid port(%d)", staticaddrs[i].port_);
      continue;
    }

    op_list.NewDelete(
        TableStaticPeer::URI, "%s = '%s' AND %s = %d",
        TableStaticPeer::COLUMN_IP, staticaddrs[i].ip_.c_str(),
        TableStaticPeer::COLUMN_PORT, staticaddrs[i].port_);
  }

  resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
  return ZISYNC_SUCCESS;
}

err_t DiscoverDataSource::SaveStaticPeers(const std::vector<IpPort> &staticaddrs) {
  IContentResolver *resolver = GetContentResolver();
  assert(resolver);
  OperationList op_list;

  op_list.NewDelete(TableStaticPeer::URI, NULL);

  for (size_t i = 0; i < staticaddrs.size(); i++) {
    if (!IsValidPort(staticaddrs[i].port_)) {
      ZSLOG_WARNING("Recv Invalid port(%d)", staticaddrs[i].port_);
      continue;
    }

    ContentOperation *cp = op_list.NewInsert(TableStaticPeer::URI, AOC_REPLACE);
    ContentValues *cv = cp->GetContentValues();

    cv->Put(TableStaticPeer::COLUMN_IP, staticaddrs[i].ip_);
    cv->Put(TableStaticPeer::COLUMN_PORT, staticaddrs[i].port_);
  }

  int32_t total_ops = op_list.GetCount();
  if (total_ops <= 0) {
    return ZISYNC_SUCCESS;
  }
  int32_t ok_ops = resolver->ApplyBatch(ContentProvider::AUTHORITY, &op_list);
  if (total_ops != ok_ops) {
    return ZISYNC_ERROR_CONTENT; 
  }else {
    return ZISYNC_SUCCESS;
  }
}

}  // namespace zs
