// Copyright 2014, zisync.com
#include "zisync/kernel/platform/platform.h"

#include <cassert>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/sync_put_handler.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/event_notifier.h"

namespace zs {

#define LOG_INFO(sync_file) \
  ZSLOG_INFO("Add SyncTask(path = (%s), sync_mask = %s)",  \
             sync_file->remote_file_stat()->path(),        \
             str_sync_mask(sync_file->mask()));

SyncPutHandler::~SyncPutHandler() {
  HandlePutFiles();
  if (error_code_ == ZISYNC_SUCCESS) {
    if (local_tree) {
      UpdateLastSync(local_tree->sync_id());
    }
  }
  zs::GetEventNotifier()->NotifyDownloadFileNumber(download_num_);
  OsDeleteDirectories(put_tmp_root_, true);
}


bool SyncPutHandler::OnHandleFile(
    const string &relative_path, const string& real_path,
    const string &sha1) {
  if (relative_path == string("/") + SYNC_FILE_TASKS_META_FILE) {
    err_t zisync_ret = ParseMetaFile();
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("ParseMetaFile fail");
      return false;
    }
    has_recv_meta_file_ = true;
    return true;
  } else {
    auto find = data_tasks.find(relative_path);
    // assert(find != data_tasks.end());
    if (find == data_tasks.end()) {
      ZSLOG_WARNING("Handle File(%s) not in SyncFiles",
                    relative_path.c_str());
      return false;
    }

    unique_ptr<SyncFile> sync_file(find->second.release());
    data_tasks.erase(find);
    assert(sync_file->remote_file_stat() != NULL);
    if (sync_file->remote_file_stat()->sha1 != sha1) {
      ZSLOG_INFO("Download file(%s) sha1(%s) diff from sha1(%s) in last find",
                 sync_file->remote_file_stat()->path(), sha1.c_str(),
                 sync_file->remote_file_stat()->sha1.c_str());
      return false;
    }
    wait_handle_files_.emplace_back(sync_file.release());
    assert(local_file_consistent_handler_);
  }

  if (has_recv_meta_file_ && 
      static_cast<int>(wait_handle_files_.size()) > APPLY_BATCH_NUM_LIMIT) {
      HandlePutFiles();
  }
  return true;
}

err_t SyncPutHandler::ParseMetaFile() {
  string meta_file = put_tmp_root_ + "/" + SYNC_FILE_TASKS_META_FILE;
  OsFile file;
  OsFileStat file_stat;
  int ret = OsStat(meta_file, string(), &file_stat);
  if (ret != 0) {
    ZSLOG_ERROR("OsStat(%s) fail : %s", meta_file.c_str(),
                OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  if (file_stat.length <= 0) {
    ZSLOG_ERROR("Invalid MetaFile length(%" PRId64")", file_stat.length);
    return ZISYNC_ERROR_OS_IO;
  }
  ret = file.Open(meta_file.c_str(), string(), "rb");
  if (ret != 0) {
    ZSLOG_ERROR("Open MetaFile(%s) fail : %s", meta_file.c_str(),
                OsGetLastErr());
    return ZISYNC_ERROR_OS_IO;
  }
  string data;
  size_t len = file.ReadWholeFile(&data);
  assert(len == data.size());
  if (static_cast<int64_t>(len) != file_stat.length) {
    ZSLOG_ERROR("Read from (%s) fail, expected len(%" PRId64 ") != "
                "len(%zd)", meta_file.c_str(), file_stat.length, len);
    return ZISYNC_ERROR_OS_IO;
  }
  if (!push_sync_meta.ParseFromString(data)) {
    ZSLOG_ERROR("Parse Protobuf from MetaFile(%s) fail.", 
                meta_file.c_str());
    return zs::ZISYNC_ERROR_INVALID_MSG;
  }

  local_tree.reset(Tree::GetBy(
          "%s = '%s' AND %s = %d AND %s = %d", 
          TableTree::COLUMN_UUID, push_sync_meta.local_tree_uuid().c_str(),
          TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!local_tree) {
    ZSLOG_ERROR("Tree(%s) does not exist", 
                push_sync_meta.local_tree_uuid().c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }
  local_tree_root = local_tree->root();
  FixTreeRoot(&local_tree_root);
  local_file_uri.reset(new Uri(TableFile::GenUri(
              push_sync_meta.local_tree_uuid().c_str())));
  local_file_authority = TableFile::GenAuthority(
      push_sync_meta.local_tree_uuid().c_str());

  remote_tree.reset(Tree::GetBy(
          "%s = '%s' AND %s = %d", 
          TableTree::COLUMN_UUID, push_sync_meta.remote_tree_uuid().c_str(),
          TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  if (!remote_tree) {
    ZSLOG_ERROR("Tree(%s) does not exist", 
                push_sync_meta.remote_tree_uuid().c_str());
    return ZISYNC_ERROR_TREE_NOENT;
  }
  if (remote_tree->sync_id() != local_tree->sync_id()) {
    ZSLOG_ERROR("Tree(%s) and Tree(%s) has different sync->id()",
                push_sync_meta.local_tree_uuid().c_str(),
                push_sync_meta.remote_tree_uuid().c_str());
    return zs::ZISYNC_ERROR_SYNCDIR_MISMATCH;
  }

  sync.reset(Sync::GetByIdWhereStatusNormal(local_tree->sync_id()));
  if (!sync || sync->perm() == TableSync::PERM_WRONLY || 
      sync->perm() == TableSync::PERM_DISCONNECT ||
      sync->perm() == TableSync::PERM_CREATOR_DELETE ||
      sync->perm() == TableSync::PERM_TOKEN_DIFF) {
    // WR not allow push
    ZSLOG_ERROR("Noent Sync(%d)", local_tree->sync_id());
    return ZISYNC_ERROR_SYNC_NOENT;
  }
  local_file_consistent_handler_.reset(
      new LocalFileConsistentHandler(*local_tree, *remote_tree, *sync));

  err_t zisync_ret = TransferRemoteFileStats();
  if (zisync_ret != ZISYNC_SUCCESS) {
    return zisync_ret;
  }
  HandleRemoteChanges();
  rm_meta_tasks.clear();
  mk_meta_tasks.clear();
  
  return ZISYNC_SUCCESS;
}

err_t SyncPutHandler::TransferRemoteFileStats() {
  // update tree_uuids
  IContentResolver *resolver = GetContentResolver();
  vector<string> local_tree_uuids, new_tree_uuids;
  const char* tree_projs[] = {
    TableTree::COLUMN_UUID, 
  };
  assert(sync->id() != -1);
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
          "%s = %" PRId32 " AND %s != '%s'", 
          TableTree::COLUMN_SYNC_ID, sync->id(), 
          TableTree::COLUMN_UUID, push_sync_meta.local_tree_uuid().c_str()));
  local_tree_uuids.push_back(push_sync_meta.local_tree_uuid());
  while (cursor->MoveToNext()) {
    local_tree_uuids.push_back(cursor->GetString(0));
  }

  const MsgRemoteMeta &remote_meta = push_sync_meta.remote_meta();
  vector<int> vclock_remote_map_to_local(remote_meta.uuids_size(), -1);
  for (int i = 0; i < remote_meta.uuids_size(); i ++) {
    auto find = std::find(local_tree_uuids.begin(), local_tree_uuids.end(), 
                          remote_meta.uuids(i));
    if (find != local_tree_uuids.end()) {
      vclock_remote_map_to_local.at(i) = std::distance(
          local_tree_uuids.begin(), find);
    } else {
      vclock_remote_map_to_local.at(i) = 
          local_tree_uuids.size() + new_tree_uuids.size();
      new_tree_uuids.push_back(remote_meta.uuids(i));
    }
  }
  OperationList op_list;
  for (auto iter = new_tree_uuids.begin(); iter != new_tree_uuids.end();
       iter ++) {
    ContentOperation *cp = op_list.NewInsert(TableTree::URI, AOC_ABORT);
    ContentValues *cv = cp->GetContentValues();
    cv->Put(TableTree::COLUMN_UUID, iter->c_str());
    cv->Put(TableTree::COLUMN_STATUS, TableTree::STATUS_VCLOCK);
    cv->Put(TableTree::COLUMN_SYNC_ID, sync->id());
    cv->Put(TableTree::COLUMN_LAST_FIND, TableTree::LAST_FIND_NONE);
  }
  int affected_row_num = resolver->ApplyBatch(ContentProvider::AUTHORITY, 
                                              &op_list);
  if (affected_row_num != static_cast<int>(new_tree_uuids.size())) {
    ZSLOG_ERROR("Insert new uuids fail.");
    return ZISYNC_ERROR_GENERAL;
  }
  op_list.Clear();
  
  // parse file_stats
  int vclock_len = local_tree_uuids.size() + new_tree_uuids.size();
  for (int i = 0; i < remote_meta.stats_size(); i ++) {
    remote_file_stats.emplace_back(new FileStat(
            remote_meta.stats(i), vclock_remote_map_to_local, vclock_len));
  }
  return ZISYNC_SUCCESS;
}

void SyncPutHandler::HandleRemoteChanges() {
  IContentResolver *resolver = GetContentResolver();
  assert(local_file_uri);
  assert(!local_file_authority.empty());
  assert(local_tree);
  assert(remote_tree);
  bool has_remote_vclock = true;
  for (auto iter = remote_file_stats.begin(); 
       iter != remote_file_stats.end(); iter ++) {
    if (!SyncList::NeedSync(local_tree->id(), (*iter)->path())) {
      continue;
    }
    unique_ptr<ICursor2> cursor(resolver->Query(
            *local_file_uri, FileStat::file_projs_with_remote_vclock, 
            FileStat::file_projs_with_remote_vclock_len,
            "%s = '%s'", TableFile::COLUMN_PATH, 
            GenFixedStringForDatabase((*iter)->path()).c_str()));
    if (cursor->MoveToNext()) {
      unique_ptr<FileStat> local_file_stat(
          new FileStat(cursor.get(), has_remote_vclock));
      if (AddSyncFile(local_file_stat.get(), (*iter).get())) {
        local_file_stats.emplace_back(local_file_stat.release());
      }
    } else {
      AddSyncFile(NULL, (*iter).get());
    }
  }
  
  class PushSyncRenameHandler : public RenameHandler {
   public:
    PushSyncRenameHandler(SyncPutHandler *put_handler):
        put_handler_(put_handler) {}
    virtual ~PushSyncRenameHandler() {};
    virtual void HandleRenameFrom(SyncFile *sync_file) {
      LOG_INFO(sync_file);
      put_handler_->rm_meta_tasks.emplace_back(sync_file);
    }
    virtual void HandleRenameTo(SyncFile *sync_file) {
      LOG_INFO(sync_file);
      assert(!put_handler_->data_tasks[
             sync_file->remote_file_stat()->path()]);
      put_handler_->data_tasks[
          sync_file->remote_file_stat()->path()].reset(sync_file);
    }
    virtual void HandleRename(
        SyncFile *sync_file_from, SyncFile *sync_file_to) {
      ZSLOG_INFO("Add SyncTaskRename(path_from = (%s), path_to = (%s)",  
                 sync_file_from->remote_file_stat()->path(),
                 sync_file_to->remote_file_stat()->path());
      SyncFileRename *sync_file = 
          new SyncFileRename(sync_file_from, sync_file_to);
      sync_file->set_local_tree_root(put_handler_->local_tree_root);
      put_handler_->rename_tasks.emplace_back(sync_file);
    }
   private:
    SyncPutHandler *put_handler_;
  };
  PushSyncRenameHandler handler(this);
  rename_manager.HandleRename(&handler);

  OperationList op_list;
  auto rename_start_iter = rename_tasks.begin();
  for (auto iter = rename_tasks.begin(); 
       iter != rename_tasks.end(); iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree->id(), remote_tree->id())) {
      break;
    }
    if ((iter - rename_start_iter) > APPLY_BATCH_NUM_LIMIT ||
        iter == rename_tasks.end() - 1) {
      for (auto handle_iter = rename_start_iter; handle_iter <= iter; 
           handle_iter ++) {
        if (local_file_consistent_handler_->Handle(&(*handle_iter))) {
            (*handle_iter)->Handle(&op_list);
        } else {
          unique_ptr<SyncFile>* renames[2] = { 
            (*handle_iter)->mutable_sync_file_from(), 
            (*handle_iter)->mutable_sync_file_to(), 
          };
          for (int i = 0; i < 2; i ++) {
            if (*renames[i]) {
              if ((*renames[i])->MaskIsMeta()) {
                (*renames[i])->Handle(&op_list);
              } // in push , put rename to data is no use
            }
          }
        }
      }
      int affected_low_num = resolver->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_low_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some PullMetaTask fail.");
        error_code_ = ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
      rename_start_iter = iter + 1;
    }
  }
#ifndef NDEBUG
  auto pre_iter = mk_meta_tasks.begin();
  for (auto iter = mk_meta_tasks.begin(); iter != mk_meta_tasks.end(); 
       iter ++) {
    if (iter != mk_meta_tasks.begin()) {
      assert(strcmp((*pre_iter)->remote_file_stat()->path(),
                    (*iter)->remote_file_stat()->path()) < 0);
    }
    pre_iter = iter;
  }
#endif
  
  /* since the mk_meta_tasks is already sort by asc and not changed by rename
   * handle so no need to sort */
  // std::sort(mk_meta_tasks.begin(), mk_meta_tasks.end(), 
  //           [] (const unique_ptr<SyncFile> &sync1, 
  //               const unique_ptr<SyncFile> &sync2) {
  //           return strcmp(sync1->remote_file_stat()->path(), 
  //                         sync2->remote_file_stat()->path()) < 0;
  //           });
  std::sort(rm_meta_tasks.begin(), rm_meta_tasks.end(), 
            [] (const unique_ptr<SyncFile> &sync1, 
                const unique_ptr<SyncFile> &sync2) {
            return strcmp(sync2->remote_file_stat()->path(), 
                          sync1->remote_file_stat()->path()) < 0;
            });
  for (auto iter = rm_meta_tasks.begin(); 
       iter != rm_meta_tasks.end(); iter ++) {
    mk_meta_tasks.emplace_back(iter->release());
  }
  assert(local_file_authority.length() != 0);
  
  auto start_iter = mk_meta_tasks.begin();
  for (auto iter = mk_meta_tasks.begin(); iter != mk_meta_tasks.end(); 
       iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree->id(), remote_tree->id())) {
      break;
    }
    if ((iter - start_iter) > APPLY_BATCH_NUM_LIMIT || 
        iter == mk_meta_tasks.end() - 1) {
      for (auto handle_iter = start_iter; handle_iter <= iter; 
           handle_iter ++) {
        if (local_file_consistent_handler_->Handle(&(*handle_iter)) && 
            (*handle_iter)->MaskIsMeta()) {
          (*handle_iter)->Handle(&op_list);
        } 
      }
      int affected_row_num = resolver->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_row_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some MetaTask fail.");
        error_code_ = ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
      start_iter = iter + 1;
    }
  }
  
  
}

bool SyncPutHandler::AddSyncFile(
    FileStat *local_file_stat, FileStat *remote_file_stat) {
  assert(remote_file_stat != NULL);

  int sync_mask = 0;
  SyncFile::SetSyncFileMask(local_file_stat, remote_file_stat, &sync_mask);

  if (SyncFile::IsBackupNotSync(
          local_file_stat, remote_file_stat, 
          sync_mask, local_tree->type())) {
    return false;
  }

  if (local_file_stat == NULL) {
    SyncFile::MaskSetInsert(&sync_mask);
  } else {
    VClockCmpResult vclock_cmp_result = 
        local_file_stat->vclock.Compare(remote_file_stat->vclock);
    if (vclock_cmp_result == VCLOCK_LESS) {
      SyncFile::MaskSetUpdate(&sync_mask);
    } else if (vclock_cmp_result == VCLOCK_CONFLICT) {
      SyncFile::MaskSetConflict(&sync_mask);
    } else {
      ZSLOG_INFO("Vclock equal or local Greater.");
      return false;
    }
  }

  if (SyncFile::MaskIsRemoteReg(sync_mask) && 
      SyncFile::MaskIsRemoteNormal(sync_mask)) {
    SyncFile::MaskSetData(&sync_mask);
  } else {
    SyncFile::MaskSetMeta(&sync_mask);
  }

  if (SyncFile::MaskIsRemoteReg(sync_mask) && 
      SyncFile::MaskIsRemoteNormal(sync_mask)) {
    if (SyncFile::MaskIsLocalReg(sync_mask) && 
        SyncFile::MaskIsLocalNormal(sync_mask) &&
        local_file_stat->sha1 == remote_file_stat->sha1) {
      SyncFile::MaskSetMeta(&sync_mask);
    } else {
      SyncFile::MaskSetData(&sync_mask);
    }
  } else {
    SyncFile::MaskSetMeta(&sync_mask);
  }

  SyncFile *sync_file = SyncFile::Create(*local_tree, *remote_tree, sync_mask);
  assert(local_tree_root.length() != 0);
  sync_file->set_local_tree_root(local_tree_root);
  sync_file->set_local_file_stat(local_file_stat);
  sync_file->set_remote_file_stat(remote_file_stat);
  sync_file->set_uri(local_file_uri.get());

  if (rename_manager.AddSyncFile(sync_file)) {
    return true;
  }

  LOG_INFO(sync_file);

  if (SyncFile::MaskIsMeta(sync_mask)) {
    // ZSLOG_INFO("is Meta");
    if (SyncFile::MaskIsRemoteRemove(sync_mask)) {
      rm_meta_tasks.emplace_back(sync_file);
    } else {
      mk_meta_tasks.emplace_back(sync_file);
    }
  } else {
    // ZSLOG_INFO("is Data");
    assert(!data_tasks[sync_file->remote_file_stat()->path()]);
    data_tasks[sync_file->remote_file_stat()->path()].reset(sync_file);
  }
  return true;
}

void SyncPutHandler::HandlePutFiles() {
  OperationList op_list;
  for (auto iter = wait_handle_files_.begin(); 
       iter != wait_handle_files_.end(); iter ++) {
    if (local_file_consistent_handler_->Handle(&(*iter))) {
      if ((*iter)->MaskIsMeta()) {
        (*iter)->Handle(&op_list);
      } else {
        (*iter)->Handle(&op_list, put_tmp_root_.c_str());
      }
    }
    if (op_list.GetCount() > APPLY_BATCH_NUM_LIMIT ||
        iter == wait_handle_files_.end() - 1) {
      int affected_row_num = GetContentResolver()->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_row_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some DataTask fail.");
        error_code_ = ZISYNC_ERROR_GENERAL;
      }
      download_num_ += affected_row_num;
      op_list.Clear();
    }
  }
  wait_handle_files_.clear();
}

}  // namespace zs
