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

#include "zisync/kernel/discover/discover_worker.h"

namespace zs {


class DiscoverDataStore : public IDiscoverDataStore {
 public:
  DiscoverDataStore();
  virtual ~DiscoverDataStore();

  virtual err_t Initialize();

  virtual int32_t GetPort();
  virtual const std::string GetMyNodeId();

  virtual err_t GetSuperNodes(std::vector<std::string>* super_nodes);
  virtual err_t GetInfoHashes(std::vector<std::string>* infohashes);
  
  virtual err_t StorePort(int32_t new_port);
  virtual void  StorePeer(const std::string& info_hash,
                          const std::string& host,
                          int port, bool is_lan, bool is_ipv6);
 private:
  const std::string GetAccount();
  err_t GetShareSyncs(std::vector<std::string>* sync_uuids);
  void IssueDeviceInfo(
      const std::string& host, int port, const char *sync_uuid,
      bool is_ipv6);
  ZmqSocket *inner_push;
};


}  // namespace zs


#endif  // ZISYNC_KERNEL_DISCOVER_DISCOVER_DATA_STORE_H_
