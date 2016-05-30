// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_WORKER_SYNC_FILE_TASK_H_
#define ZISYNC_KERNEL_WORKER_SYNC_FILE_TASK_H_

#include <string>
#include <vector>
#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/worker/sync_file.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/rename.h"
#include "zisync/kernel/proto/kernel.pb.h"

namespace zs {

using std::string;
using std::vector;
using std::unique_ptr;

class FileStat;
class FilterPushSyncMetaResponse;

enum SyncFileTaskMode {
  SYNC_FILE_TASK_MODE_PUSH,
  SYNC_FILE_TASK_MODE_PULL,
};

class SyncFileTask {
  friend class PullRenameHandler;
  friend class PushRenameHandler;
 public :
  SyncFileTask(const Tree &local_tree, const Tree &remote_tree,
               const Sync &sync);
  /*  todo the local vclock of both stat should be the same */
  bool Add(FileStat *local_stat, FileStat *remote_stat);
  void set_local_tree_uuids(const vector<string> *local_tree_uuids_) {
    local_tree_uuids = local_tree_uuids_;
  }
  void set_remote_device_ip(const char *device_ip);
  err_t Run();
  void Prepare() {
    HandlePullRename();
    FilterPushTasks();
    HandlePushRename();
  }
  bool IsAllSucces() {
    return error == ZISYNC_SUCCESS;
  }

  void set_remote_device(const Device *remote_device_) {
    remote_device = remote_device_;
  }
  int32_t num_file_to_upload() { return  push_data_tasks.size(); }
  int32_t num_file_to_download() { return pull_data_tasks.size(); }
  int32_t num_file_consistent() { return num_file_consistent_; }
  int64_t num_byte_to_upload();
  int64_t num_byte_to_download();
  int64_t num_byte_consistent() { return num_byte_consistent_; }
  
 private:
  SyncFileTask(SyncFileTask&);
  void operator=(SyncFileTask&);

  bool HandleLocalFileConsistent(unique_ptr<SyncFile> *sync_file);
  void HandlePullRename();
  void HandlePullMetaTasks();
  void FilterPushTasks();
  void HandlePushRename();
  void HandlePullDataTasks();
  void HandlePushTasks();
  err_t FilterPushMeta(
      const vector<unique_ptr<SyncFile>> &tasks, 
      FilterPushSyncMetaResponse *response);
  void AddPushTask(SyncFile *sync_file);

  vector<unique_ptr<SyncFile>> pull_mk_meta_tasks, pull_rm_meta_tasks, 
      pull_data_tasks, push_tasks, push_data_tasks, push_meta_tasks;
  vector<unique_ptr<SyncFileRename>> pull_rename_tasks; 
  int32_t num_file_consistent_;
  int64_t num_byte_consistent_;
  const Tree &local_tree, &remote_tree;
  const Sync &sync;
  const Device *remote_device;
  OperationList op_list;
  const vector<string> *local_tree_uuids; // not add the new_tree_uuids
  string local_file_authority;
  const Uri local_file_uri;
  int32_t local_vclock_index_of_local_tree, local_vclock_index_of_remote_tree;
  string remote_device_route_uri, remote_device_data_uri;
  unique_ptr<LocalFileConsistentHandler> local_file_consistent_handler;

  err_t error;
  MsgPushSyncMeta push_sync_meta;

  RenameManager pull_rename_manager, push_rename_manager;
};

}   // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_SYNC_FILE_TASK_H_

