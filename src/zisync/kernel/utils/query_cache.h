// Copyright 2015, zisync.com

#ifndef ZISYNC_KERNEL_WORKER_QUERY_CACHE_WORKER_H_
#define ZISYNC_KERNEL_WORKER_QUERY_CACHE_WORKER_H_

#include <map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/proto/kernel.pb.h"

namespace zs {

class MessageHandler;
class ZmqSocket;

class QueryCache : public OsThread {
  friend class QueryCacheUpdateHandler;
 public:
  virtual ~QueryCache();

  static QueryCache* GetInstance() { return &query_cache; }

  err_t QuerySyncInfo(QuerySyncInfoResult *result) const;
  err_t QuerySyncInfo(int32_t sync_id, SyncInfo *sync_info) const;
  err_t QueryBackupInfo(QueryBackupInfoResult *result) const;
  err_t QueryBackupInfo(int32_t backup_id, BackupInfo *backup_info) const;
  
  void NotifyModify();
  err_t Startup();
  void Shutdown();

 private:
  QueryCache();
  QueryCache(QueryCache&);
  void operator=(QueryCache&);

  void QueryCacheUpdate();

  virtual int Run();

  static QueryCache query_cache;

  ZmqSocket *pull, *exit;
  std::map<MsgCode, MessageHandler*> pull_handler_map_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_QUERY_CACHE_WORKER_H_
