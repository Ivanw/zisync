// Copyright 2015, zisync.com
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sync_get_handler.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/sync_const.h"

namespace zs {

void SyncGetHandler::OnHandleFile(
    const string &relative_path, const string &sha1) {
  auto find = sync_files_.find(relative_path);
  assert(find != sync_files_.end());
  if (find == sync_files_.end()) {
    ZSLOG_WARNING("Handle File(%s) not in SyncFiles, this should not happend",
                  relative_path.c_str());
    return;
  }

  unique_ptr<SyncFile> sync_file(find->second.release());
  sync_files_.erase(find);
  assert(sync_file->remote_file_stat() != NULL);
  if (sync_file->remote_file_stat()->sha1 != sha1) {
    ZSLOG_INFO("Download file(%s) sha1(%s) diff from sha1(%s) in last find",
               sync_file->remote_file_stat()->path(), sha1.c_str(),
               sync_file->remote_file_stat()->sha1.c_str());
    return;
  }
  wait_handle_files_.emplace_back(sync_file.release());
  assert(local_file_consistent_handler_);
  if (static_cast<int>(wait_handle_files_.size()) > APPLY_BATCH_NUM_LIMIT) {
    HandleGetFiles();
  }
}

void SyncGetHandler::HandleGetFiles() {
  OperationList op_list;
  for (auto iter = wait_handle_files_.begin(); 
       iter != wait_handle_files_.end(); iter ++) {
    if (local_file_consistent_handler_->Handle(&(*iter))) {
      if ((*iter)->MaskIsMeta()) {
        (*iter)->Handle(&op_list);
      } else {
        (*iter)->Handle(&op_list, get_tmp_path_.c_str());
      }
    }
  }
  int affected_row_num = GetContentResolver()->ApplyBatch(
      local_file_authority_.c_str(), &op_list);
  if (affected_row_num != op_list.GetCount()) {
    ZSLOG_ERROR("Some PullDataTask fail when local handle");
    error_code_ = ZISYNC_ERROR_GENERAL;
  }
  downlaod_num_ += affected_row_num;
  wait_handle_files_.clear();
}

void SyncGetHandler::AppendSyncFile(SyncFile *sync_file) {
  assert(sync_file->remote_file_stat() != NULL);
  assert(sync_files_.find(sync_file->remote_file_stat()->path()) 
         == sync_files_.end());
  sync_files_[sync_file->remote_file_stat()->path()].reset(sync_file);
}

}  // namespace zs
