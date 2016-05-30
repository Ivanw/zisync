// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_SYNC_PUT_HANDLER_H_
#define ZISYNC_KERNEL_SYNC_PUT_HANDLER_H_

#include <memory>

#include "zisync_kernel.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/worker/sync_file.h"
#include "zisync/kernel/utils/rename.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/tree.h"

namespace zs {

using std::string;
using std::vector;
using std::unique_ptr;
using std::map;

class SyncPutHandler : public IPutHandler {
 public:
  SyncPutHandler(const string& tmp_root):put_tmp_root_(tmp_root),
    has_recv_meta_file_(false), error_code_(ZISYNC_SUCCESS), download_num_(0) {}
  virtual ~SyncPutHandler();

  virtual bool OnHandleFile(
      const std::string& relative_path, 
      const std::string& real_path, const std::string& sha1);
  
 private: 
  SyncPutHandler(SyncPutHandler&);
  void operator=(SyncPutHandler&);
  err_t ParseMetaFile();
  err_t TransferRemoteFileStats();
  void HandleRemoteChanges();
  bool AddSyncFile(
    FileStat *local_file_stat, FileStat *remote_file_stat);
  void HandlePutFiles();

  string put_tmp_root_;
  bool has_recv_meta_file_;
  MsgPushSyncMeta push_sync_meta;
  vector<unique_ptr<SyncFile>> wait_handle_files_;
  string local_tree_root, local_file_authority;
  unique_ptr<Tree> local_tree, remote_tree;
  err_t error_code_;
  unique_ptr<Uri> local_file_uri;
  vector<unique_ptr<FileStat>> remote_file_stats, local_file_stats;
  vector<unique_ptr<SyncFile>> rm_meta_tasks, mk_meta_tasks;
  vector<unique_ptr<SyncFileRename>> rename_tasks;
  map<string /* relative_path */, unique_ptr<SyncFile>> data_tasks;
  unique_ptr<Sync> sync;
  unique_ptr<LocalFileConsistentHandler> local_file_consistent_handler_;
  RenameManager rename_manager;  
  int32_t download_num_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_SYNC_PUT_HANDLER_H_
