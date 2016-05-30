// Copyright 2014, zisync.com

#ifndef ZISYNC_KERNEL_UTILS_SYNC_UPDATER_H_
#define ZISYNC_KERNEL_UTILS_SYNC_UPDATER_H_

#include <memory>

#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/worker/sync_file_task.h"

namespace zs {

using std::unique_ptr;

class SyncFileTask;
class Tree;
class Sync;

class SyncUpdater {
 public:
  SyncUpdater(int32_t local_tree_id, int32_t remote_tree_id);

  /* call before update */
  bool Initialize();
  /* compare FileStats in db, add SyncFile in sync_file_task,
   * UpdateTreePairStatus*/
  void Update(const Device *remote_device = NULL, 
              const char *remote_device_ip = NULL);
  SyncFileTask* sync_file_task() { 
    assert(sync_file_task_); return sync_file_task_.get(); }
  const Tree& local_tree() { assert(local_tree_); return *local_tree_; }
  const Tree& remote_tree() { assert(remote_tree_); return *remote_tree_; }
  const Sync& sync() { assert(sync_); return *sync_; }
 private:
  void LocalNewFile();
  void RemoteNewFile();
  void UpdateFile();
  void SetSyncFileTask();
  void UpdateTreePairStatus();
  
  int32_t local_tree_id_, remote_tree_id_;
  
  unique_ptr<Tree> local_tree_, remote_tree_;
  unique_ptr<Sync> sync_;
  unique_ptr<SyncFileTask> sync_file_task_;
  
  std::vector<std::string> local_tree_uuids;
  std::unique_ptr<ICursor2> local_cursor;
  std::unique_ptr<ICursor2> remote_cursor;
  std::unique_ptr<FileStat> local_file_stat;
  std::unique_ptr<FileStat> remote_file_stat;
  std::vector<std::unique_ptr<FileStat>> local_file_stats, remote_file_stats;
  std::vector<int> remote_vclock_map_to_local;
};

}  // namespace zs
#endif  // ZISYNC_KERNEL_UTILS_SYNC_UPDATER_H_
