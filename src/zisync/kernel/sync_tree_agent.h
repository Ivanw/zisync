// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_WORKER_SYNC_TREE_AGENT_H_
#define ZISYNC_KERNEL_WORKER_SYNC_TREE_AGENT_H_

#include "zisync/kernel/libevent/transfer.h"

namespace zs {

class SyncTreeAgent : public ITreeAgent {
 public:
  virtual std::string GetTreeRoot(const std::string& tree_uuid);
  virtual std::string GetNewTmpDir(const std::string& tree_uuid);
  virtual int32_t GetTreeId(const std::string& tree_uuid);
  virtual int32_t GetSyncId(const std::string& tree_uuid);
  virtual std::string GetTreeUuid(const int32_t tree_id);
  virtual bool AllowPut(
      int32_t local_tree_id, int32_t remote_tree_id, 
      const std::string& relative_path);
  virtual bool TryLock(int32_t local_tree_id, int32_t remote_tree_id);
  virtual void Unlock(int32_t local_tree_id, int32_t remote_tree_id);
  virtual IPutHandler* CreatePutHandler(
      const std::string &tmp_root);
  virtual IPutHandler* CreateUploadHandler(
      const std::string &tree_uuid, const std::string &tmp_root) ;
  virtual ITaskMonitor* CreateTaskMonitor(
      TaskType type,
      const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid,
      int32_t total_files, int64_t total_bytes);

  virtual std::string GetAlias(
      const std::string& tree_uuid, const std::string& tree_root, 
      const std::string &relative_path);

  static SyncTreeAgent* GetInstance() {
    return &sync_tree_agent;
  }
 private:
  SyncTreeAgent() {}
  SyncTreeAgent(SyncTreeAgent&);
  void operator=(SyncTreeAgent&);

  static SyncTreeAgent sync_tree_agent;
};


}  // namespace zs


#endif  // ZISYNC_KERNEL_WORKER_SYNC_TREE_AGENT_H_
