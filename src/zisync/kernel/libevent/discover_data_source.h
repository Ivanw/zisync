/**
 * @file discover_data_store.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief data data store.
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

#ifndef ZISYNC_KERNEL_DISCOVER_DISCOVER_DATA_STORE_H_
#define ZISYNC_KERNEL_DISCOVER_DISCOVER_DATA_STORE_H_

#include "zisync/kernel/libevent/discover_server.h"
#include "zisync/kernel/database/content_resolver.h"

namespace zs {

class StaticPeerObserver : public ContentObserver {
 public:
  StaticPeerObserver();
  ~StaticPeerObserver();
  //
  // Implement ContentObserver
  virtual void* OnQueryChange();
  virtual void  OnHandleChange(void* lpChanges);
  err_t GetStaticPeers(std::vector<IpPort> *staticaddrs);
  int32_t peers_size() {
    MutexAuto mutex_auto(&peers_mutex_);
    return peers_.size();
  }

 private:
  std::vector<IpPort> peers_;
  OsMutex peers_mutex_;
};

class DiscoverDataSource : public IDiscoverDataSource
                         , public ContentObserver {
 public:
  DiscoverDataSource();
  virtual ~DiscoverDataSource();
  //
  // Implement ContentObserver
  virtual void* OnQueryChange();
  virtual void  OnHandleChange(void* lpChanges);

  virtual int32_t GetDiscoverPort();
  virtual int32_t GetRoutePort();
  virtual const std::string GetMyNodeId();

  virtual err_t GetTrackers(std::vector<std::string>* super_nodes);
  virtual err_t GetInfoHashes(std::vector<std::string>* infohashes);
  
  virtual err_t StorePort(int32_t new_port);
  virtual void  StorePeers(const std::string infohash, const bool is_ipv6,
                           const std::vector<SearchNode> &peers);
  virtual const std::string GetAccount();
  virtual err_t GetShareSyncs(std::vector<std::string>* sync_uuids);

  virtual err_t GetStaticPeers(std::vector<IpPort> *staticaddrs);
  virtual err_t StoreStaticPeers(const std::vector<IpPort> &staticaddrs);
  virtual err_t DeleteStaticPeers(const std::vector<IpPort> &staticaddrs);
  virtual err_t SaveStaticPeers(const std::vector<IpPort> &staticaddrs);

 private:
  std::vector<std::string> infohashes_;
  OsMutex infohashes_mutex_;

  StaticPeerObserver* static_peer_source_;
};


}  // namespace zs


#endif  // ZISYNC_KERNEL_DISCOVER_DISCOVER_DATA_STORE_H_
