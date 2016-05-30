// Copyright 2014, zisync.com

#include <cassert>
#include <memory>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4244)
#pragma warning( disable : 4267)
#include "zisync/kernel/proto/kernel.pb.h"
#pragma warning( pop )
#else
#include "zisync/kernel/proto/kernel.pb.h"
#endif

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/worker/sync_file.h"
#include "zisync/kernel/worker/sync_file_task.h"
#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/response.h"

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/status.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/sync_get_handler.h"
#include "zisync/kernel/utils/tree_mutex.h"

namespace zs {

using std::unique_ptr;

#define LOG_INFO(sync_file, sync_mode) \
ZSLOG_INFO("Add SyncTask(path = (%s), "            \
           "sync_mask = %s, sync_mode = %s",       \
           sync_file->local_file_stat() == NULL ?  \
           sync_file->remote_file_stat()->path() : \
           sync_file->local_file_stat()->path(),   \
           str_sync_mask(sync_file->mask()),       \
           sync_mode == SYNC_FILE_TASK_MODE_PULL ? \
           "SYNC_FILE_TASK_MODE_PULL" : "SYNC_FILE_TASK_MODE_PUSH")

#define RENAME_LOG_INFO(sync_file_from, sync_file_to, sync_mode)  \
ZSLOG_INFO("Add SyncTaskRename(path_from = (%s), path_to = (%s), "  \
           "sync_mode = %s",                      \
           sync_file_from->local_file_stat() == NULL ?            \
           sync_file_from->remote_file_stat()->path() :           \
           sync_file_from->local_file_stat()->path(),             \
           sync_file_to->local_file_stat() == NULL ?              \
           sync_file_to->remote_file_stat()->path() :             \
           sync_file_to->local_file_stat()->path(),               \
           sync_mode == SYNC_FILE_TASK_MODE_PULL ?                \
           "SYNC_FILE_TASK_MODE_PULL" : "SYNC_FILE_TASK_MODE_PUSH")

void SyncFileTask::set_remote_device_ip(const char *device_ip) {
  assert(remote_device);
  StringFormat(&remote_device_route_uri, "tcp://%s:%d",
               device_ip, remote_device->route_port());
  StringFormat(&remote_device_data_uri, "tcp://%s:%d",
               device_ip, remote_device->data_port());
}

err_t SyncFileTask::Run() {
  assert(remote_device);
  assert(remote_device_route_uri.length() != 0);
  assert(remote_device_data_uri.length() != 0);
  if (TreeMutex::TryLock(local_tree.id(), local_tree.id())) {
    HandlePullMetaTasks();
    HandlePullDataTasks();
    TreeMutex::Unlock(local_tree.id(), local_tree.id());
  }
  HandlePushTasks();
  return error;
}

SyncFileTask::SyncFileTask(
    const Tree &local_tree_, const Tree &remote_tree,
    const Sync &sync):
    num_file_consistent_(0), num_byte_consistent_(0),
    local_tree(local_tree_), remote_tree(remote_tree),
    sync(sync), remote_device(NULL),
    local_tree_uuids(NULL), 
    local_file_authority(TableFile::GenAuthority(local_tree.uuid().c_str())), 

    local_file_uri(TableFile::GenUri(local_tree.uuid().c_str())),
    local_vclock_index_of_local_tree(-1), 
    local_vclock_index_of_remote_tree(-1), 
    local_file_consistent_handler(new LocalFileConsistentHandler(
            local_tree, remote_tree, sync)),
    error(ZISYNC_SUCCESS) {
    }

bool SyncFileTask::Add(FileStat *local_file_stat, 
                       FileStat *remote_file_stat) {

  SyncFileTaskMode sync_mode;
  int sync_mask = 0;

  if (local_file_stat == NULL) {
    sync_mode = SYNC_FILE_TASK_MODE_PULL;
    SyncFile::MaskSetInsert(&sync_mask);
  } else if (remote_file_stat == NULL) {
    sync_mode = SYNC_FILE_TASK_MODE_PUSH;
    SyncFile::MaskSetInsert(&sync_mask);
  } else {
    VClockCmpResult vclock_cmp_result = 
        local_file_stat->vclock.Compare(remote_file_stat->vclock);
    if (vclock_cmp_result == VCLOCK_EQUAL) {
//      ZSLOG_INFO("Vclock equal.");
      if (local_file_stat->type == OS_FILE_TYPE_REG) {
        num_file_consistent_ ++;
        num_byte_consistent_ += local_file_stat->length;
      }
      return false;
    } else if (vclock_cmp_result == VCLOCK_GREATER) {
      sync_mode = SYNC_FILE_TASK_MODE_PUSH;
      SyncFile::MaskSetUpdate(&sync_mask);
    } else if (vclock_cmp_result == VCLOCK_LESS) {
      sync_mode = SYNC_FILE_TASK_MODE_PULL;
      SyncFile::MaskSetUpdate(&sync_mask);
    } else if (vclock_cmp_result == VCLOCK_CONFLICT) {
      sync_mode = SYNC_FILE_TASK_MODE_PULL;
      SyncFile::MaskSetConflict(&sync_mask);
    } else {
      assert(false);
    }
  }

  if (sync_mode == SYNC_FILE_TASK_MODE_PULL) {
    if (sync.perm() == SYNC_PERM_WRONLY) {
      return false;
    }
  } else {
    assert(sync_mode == SYNC_FILE_TASK_MODE_PUSH);
    if (sync.perm() == SYNC_PERM_RDONLY) {
      return false;
    }
  }

  int32_t local_tree_backup_type_;
  FileStat *local_file_stat_, *remote_file_stat_;
  if (sync_mode == SYNC_FILE_TASK_MODE_PULL) {
    SyncFile::SetSyncFileMask(local_file_stat, remote_file_stat, &sync_mask);
    local_tree_backup_type_ = local_tree.type();
    local_file_stat_ = local_file_stat;
    remote_file_stat_ = remote_file_stat;
  } else {
    SyncFile::SetSyncFileMask(remote_file_stat, local_file_stat, &sync_mask);
    local_tree_backup_type_ = remote_tree.type();
    local_file_stat_ = remote_file_stat;
    remote_file_stat_ = local_file_stat;
  }

  if (SyncFile::IsBackupSrcRemove(
          local_file_stat_, remote_file_stat_, 
          sync_mask, local_tree_backup_type_)) {
    if (local_tree_backup_type_ == TableTree::BACKUP_DST) { 
      if (local_file_stat_ != NULL && 
          SyncFile::MaskIsLocalNormal(sync_mask)) { 
        if (local_file_stat->type == OS_FILE_TYPE_REG) {
          num_file_consistent_ ++; 
          num_byte_consistent_ += local_file_stat_->length; 
        } 
      }
    } else { 
      if (SyncFile::MaskIsRemoteNormal(sync_mask)) {
        if (remote_file_stat->type == OS_FILE_TYPE_REG) {
          num_file_consistent_ ++;
          num_byte_consistent_ += remote_file_stat_->length;
        }
      }
    }
    return false;
  } else if (SyncFile::IsBackupDstInsert(
          local_file_stat_, remote_file_stat_, 
          sync_mask, local_tree_backup_type_)) {
    return false;
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

  SyncFile *sync_file = SyncFile::Create(
      local_tree, remote_tree, *remote_device, sync_mask);
  sync_file->set_local_file_stat(local_file_stat_);
  sync_file->set_remote_file_stat(remote_file_stat_);

  if (sync_mode == SYNC_FILE_TASK_MODE_PULL) {
    sync_file->set_local_tree_root(local_tree.root());
    sync_file->set_uri(&local_file_uri);
    if (pull_rename_manager.AddSyncFile(sync_file)) {
      return true;
    }
  } else {
    push_tasks.emplace_back(sync_file);
    return true;
  }

  assert(sync_mode == SYNC_FILE_TASK_MODE_PULL);
  if (SyncFile::MaskIsMeta(sync_mask)) {
    // ZSLOG_INFO("is Meta");
    if (SyncFile::MaskIsRemoteRemove(sync_mask)) {
      pull_rm_meta_tasks.emplace_back(sync_file);
    } else {
      pull_mk_meta_tasks.emplace_back(sync_file);
    }
  } else {
    // ZSLOG_INFO("is Data");
    pull_data_tasks.emplace_back(sync_file);
  }

  LOG_INFO(sync_file, sync_mode);

  return true;
}

void SyncFileTask::HandlePullRename() {
  class PullRenameHandler : public RenameHandler {
   public:
    PullRenameHandler(SyncFileTask *sync_file_task):
        sync_file_task_(sync_file_task) {}
    virtual ~PullRenameHandler() {};
    virtual void HandleRenameFrom(SyncFile *sync_file) {
      LOG_INFO(sync_file, SYNC_FILE_TASK_MODE_PULL);
      sync_file_task_->pull_rm_meta_tasks.emplace_back(sync_file);

    }
    virtual void HandleRenameTo(SyncFile *sync_file) {
      LOG_INFO(sync_file, SYNC_FILE_TASK_MODE_PULL);
      sync_file_task_->pull_data_tasks.emplace_back(sync_file);
    }
    virtual void HandleRename(
        SyncFile *sync_file_from, SyncFile *sync_file_to) {
      RENAME_LOG_INFO(sync_file_from, sync_file_to, SYNC_FILE_TASK_MODE_PULL);
      SyncFileRename *sync_file = 
          new SyncFileRename(sync_file_from, sync_file_to);
      sync_file->set_local_tree_root(sync_file_task_->local_tree.root());
      sync_file_task_->pull_rename_tasks.emplace_back(sync_file);
    }
   private:
    SyncFileTask *sync_file_task_;
  };
  PullRenameHandler handler(this);
  pull_rename_manager.HandleRename(&handler);
}

void SyncFileTask::HandlePullMetaTasks() {
  IContentResolver *resolver = GetContentResolver();
  assert(op_list.GetCount() == 0);
#ifndef NDEBUG
  auto pre_iter = pull_mk_meta_tasks.begin();
  for (auto iter = pull_mk_meta_tasks.begin(); 
       iter != pull_mk_meta_tasks.end(); iter ++) {
    if (iter != pull_mk_meta_tasks.begin()) {
      assert(strcmp((*pre_iter)->remote_file_stat()->path(),
                    (*iter)->remote_file_stat()->path()) < 0);
    }
    pre_iter = iter;
  }
#endif
  /* since the mk_meta_tasks is already sort by asc and not changed by rename
   * handle so no need to sort */
  // std::sort(pull_mk_meta_tasks.begin(), pull_mk_meta_tasks.end(), 
  //           [] (const unique_ptr<SyncFile> &sync1, 
  //               const unique_ptr<SyncFile> &sync2) {
  //           return strcmp(sync1->remote_file_stat()->path(), 
  //                         sync2->remote_file_stat()->path()) < 0;
  //           });
  std::sort(pull_rm_meta_tasks.begin(), pull_rm_meta_tasks.end(), 
            [] (const unique_ptr<SyncFile> &sync1, 
                const unique_ptr<SyncFile> &sync2) {
            return strcmp(sync2->remote_file_stat()->path(), 
                          sync1->remote_file_stat()->path()) < 0;
            });
  for (auto iter = pull_rm_meta_tasks.begin(); 
       iter != pull_rm_meta_tasks.end(); iter ++) {
    pull_mk_meta_tasks.emplace_back(iter->release());
  }
  auto start_iter = pull_mk_meta_tasks.begin();
  for (auto iter = pull_mk_meta_tasks.begin(); iter != pull_mk_meta_tasks.end(); 
       iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    if ((iter - start_iter) > APPLY_BATCH_NUM_LIMIT ||
        iter == pull_mk_meta_tasks.end() - 1) {
      for (auto handle_iter = start_iter; handle_iter <= iter; 
           handle_iter ++) {
        assert(local_file_consistent_handler);
        if (local_file_consistent_handler->Handle(&(*handle_iter))) {
          if ((*handle_iter)->MaskIsMeta()) {
            (*handle_iter)->Handle(&op_list);
          } else {
            pull_data_tasks.emplace_back(handle_iter->release());
          }
        }
      }
      int affected_low_num = resolver->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_low_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some PullMetaTask fail.");
        error = ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
      start_iter = iter + 1;
    }
  }

  auto rename_start_iter = pull_rename_tasks.begin();
  for (auto iter = pull_rename_tasks.begin(); 
       iter != pull_rename_tasks.end(); iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    if ((iter - rename_start_iter) > APPLY_BATCH_NUM_LIMIT ||
        iter == pull_rename_tasks.end() - 1) {
      for (auto handle_iter = rename_start_iter; handle_iter <= iter; 
           handle_iter ++) {
        assert(local_file_consistent_handler);
        if (local_file_consistent_handler->Handle(&(*handle_iter))) {
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
              } else {
                pull_data_tasks.emplace_back(renames[i]->release());
              }
            }
          }
        }
      }
      int affected_low_num = resolver->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_low_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some PullMetaTask fail.");
        error = ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
      rename_start_iter = iter + 1;
    }
  }
}

/*  overwrite */
err_t CreateFile(const string &path, const string &content) {
  OsFile file;
  int ret = file.Open(path.c_str(), string(), "wb"); 
  if (ret != 0) {
    return ZISYNC_ERROR_OS_IO;
  }

  size_t size = file.Write(content);
  if (size != content.size()) {
    return ZISYNC_ERROR_OS_IO;
  }
  return ZISYNC_SUCCESS;
}

static inline void SetMsgStatByFileStat(
    MsgStat *msg_stat, const FileStat &file_stat) {
  msg_stat->set_path(file_stat.path());
  msg_stat->set_type(file_stat.type == OS_FILE_TYPE_DIR ? FT_DIR : FT_REG);
  msg_stat->set_status(file_stat.status == TableFile::STATUS_NORMAL ? 
                       FS_NORMAL : FS_REMOVE);
  msg_stat->set_mtime(file_stat.mtime);
  msg_stat->set_length(file_stat.length);
  msg_stat->set_usn(file_stat.usn);
  msg_stat->set_sha1(file_stat.sha1);
  msg_stat->set_win_attr(file_stat.win_attr);
  msg_stat->set_unix_attr(file_stat.unix_attr);
  msg_stat->set_android_attr(file_stat.android_attr);
  msg_stat->set_modifier(file_stat.modifier);
  msg_stat->set_time_stamp(file_stat.time_stamp);
  for (int i = 0; i < file_stat.vclock.length(); i ++) {
    msg_stat->add_vclock(file_stat.vclock.at(i));
  }
}

class PushSyncMetaDeleter {
 public:
  PushSyncMetaDeleter(const string &path):meta_file_path_(path) {}
  ~PushSyncMetaDeleter() {
    zs::OsDeleteFile(meta_file_path_, false);
  }
 private:
  string meta_file_path_;
};

class TmpDirDeleter {
 public:
  TmpDirDeleter(const string &path):tmp_path_(path) {}
  ~TmpDirDeleter() {
    zs::OsDeleteDirectories(tmp_path_, true);
  }
 private:
  string tmp_path_;
};

err_t SyncFileTask::FilterPushMeta(
    const vector<unique_ptr<SyncFile>> &tasks, 
    FilterPushSyncMetaResponse *response) {
  ZmqSocket req(GetGlobalContext(), ZMQ_REQ);
  string src_uuid;
  err_t zisync_ret = req.Connect(remote_device_route_uri.c_str());
  assert(zisync_ret == ZISYNC_SUCCESS);

  FilterPushSyncMetaRequest request;
  MsgFilterPushSyncMetaRequest *msg_request = request.mutable_request();
  msg_request->set_local_tree_uuid(remote_tree.uuid());
  for (auto iter = tasks.begin(); iter != tasks.end(); iter ++) {
    SetMsgStatByFileStat(
        msg_request->add_stats(), *(*iter)->remote_file_stat());
  }
  zisync_ret = request.SendTo(req);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = response->RecvFrom(
      req, WAIT_RESPONSE_TIMEOUT_IN_S * 1000, &src_uuid);
  if (zisync_ret == ZISYNC_ERROR_TIMEOUT) {
    ZSLOG_ERROR("Recv response from (%s) timeout", 
                remote_device_route_uri.c_str());
    return  ZISYNC_ERROR_GENERAL;
  } else if (zisync_ret != ZISYNC_SUCCESS) {
    if (zisync_ret != ZISYNC_ERROR_PERMISSION_DENY) {
      ZSLOG_ERROR("Recv response from (%s) fail : %s", 
                  remote_device_route_uri.c_str(), 
                  zisync_strerror(zisync_ret));
    }
    return ZISYNC_ERROR_GENERAL;
  }

  return ZISYNC_SUCCESS;
}

void SyncFileTask::AddPushTask(SyncFile *sync_file) {
  if (!push_rename_manager.AddSyncFile(sync_file)) {
    if (SyncFile::MaskIsMeta(sync_file->mask())) {
      push_meta_tasks.emplace_back(sync_file);
    } else {
      push_data_tasks.emplace_back(sync_file);
    }
  }
}

void SyncFileTask::FilterPushTasks() {
  if (remote_device != NULL) {
    FilterPushSyncMetaResponse response;
    err_t zisync_ret = FilterPushMeta(push_tasks, &response);
    if (zisync_ret != ZISYNC_SUCCESS) {
      if (zisync_ret != ZISYNC_ERROR_PERMISSION_DENY) {
        error = zisync_ret;
        ZSLOG_ERROR("FilterPushMeta fail : %s", zisync_strerror(zisync_ret));
      }
      return;
    }
    const MsgFilterPushSyncMetaResponse &msg_response = response.response();
    auto iter = push_tasks.begin();
    for (int i = 0; i < msg_response.stats_size(); i ++) {
      assert(iter != push_tasks.end());
      assert((*iter)->remote_file_stat() != NULL);
      int ret = msg_response.stats(i).path().compare(
          (*iter)->remote_file_stat()->path());
      assert(ret >= 0);
      if (ret == 0) {//todo:bug
        AddPushTask(iter->release());
        iter ++;
      } 
    }
  } else {
    for (auto iter = push_tasks.begin(); iter != push_tasks.end();
         iter ++) {
      AddPushTask(iter->release());
    }
  }
  push_tasks.clear();
}

void SyncFileTask::HandlePushRename() {
  class PushRenameHandler : public RenameHandler {
   public:
    PushRenameHandler(SyncFileTask *sync_file_task):
        sync_file_task_(sync_file_task) {}
    virtual ~PushRenameHandler() {};
    virtual void HandleRenameFrom(SyncFile *sync_file) {
      LOG_INFO(sync_file, SYNC_FILE_TASK_MODE_PUSH);
      sync_file_task_->push_meta_tasks.emplace_back(sync_file);
    }
    virtual void HandleRenameTo(SyncFile *sync_file) {
//      LOG_INFO(sync_file, SYNC_FILE_TASK_MODE_PUSH);
      sync_file_task_->push_data_tasks.emplace_back(sync_file);
    }
    virtual void HandleRename(
        SyncFile *sync_file_from, SyncFile *sync_file_to) {
      RENAME_LOG_INFO(sync_file_from, sync_file_to, SYNC_FILE_TASK_MODE_PUSH);
      sync_file_task_->push_meta_tasks.emplace_back(sync_file_from),
          sync_file_task_->push_meta_tasks.emplace_back(sync_file_to);
    }
   private:
    SyncFileTask *sync_file_task_;
  };

  PushRenameHandler handler(this);
  push_rename_manager.HandleRename(&handler);
}

void SyncFileTask::HandlePullDataTasks() {
  ITransferServer *transfer_server = GetTransferServer2();
  IContentResolver *resolver = GetContentResolver();

  //todo: why data tasks become meta tasks?
  vector<unique_ptr<SyncFile>> data_tasks;
  vector<unique_ptr<SyncFile>> meta_tasks;
  for (auto iter = pull_data_tasks.begin(); iter != pull_data_tasks.end();
       iter ++) {
    assert(local_file_consistent_handler);
    if (local_file_consistent_handler->Handle(&(*iter))) {
      if ((*iter)->MaskIsData()) {
        data_tasks.emplace_back(iter->release());
      } else {
        meta_tasks.emplace_back(iter->release());
      }
    } else {
      ZSLOG_INFO("file(%s) is not consistent", 
                 (*iter)->remote_file_stat()->path());
    }
  }

  // handle the change back to meta tasks
  OperationList op_list;
  for (auto iter = meta_tasks.begin(); iter != meta_tasks.end(); iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    (*iter)->Handle(&op_list);
    if (op_list.GetCount() > APPLY_BATCH_NUM_LIMIT || 
        iter == meta_tasks.end() - 1) {
      int affected_row_num = resolver->ApplyBatch(
          local_file_authority.c_str(), &op_list);
      if (affected_row_num != op_list.GetCount()) {
        ZSLOG_ERROR("Some PullMetaTask fail when local handle");
        error = ZISYNC_ERROR_GENERAL;
      }
      op_list.Clear();
    }
  }

  if (data_tasks.empty()) {
    return;
  }

  int32_t total_files = 0;
  int64_t total_bytes = 0;
  for (auto iter = data_tasks.begin(); iter != data_tasks.end(); iter++) {
    total_bytes += (*iter)->remote_file_stat()->length;
    total_files ++;
  }
  TaskMonitor monitor(
      local_tree.id(), remote_tree.id(), ST_GET, total_files, total_bytes);

  for (auto iter = data_tasks.begin(); iter != data_tasks.end(); iter++) {
    std::string file_name = (*iter)->remote_file_stat()->path();
    int64_t length = (*iter)->remote_file_stat()->length;
    std::string local_file_path = local_tree.root() + "/" + file_name;
    std::string remote_file_path = remote_tree.root() + "/" + file_name;

    monitor.AppendFile(
        local_file_path, remote_file_path, file_name, length);
  }

  assert(op_list.GetCount() == 0);

  string tmp_path = local_tree.root() + "/" + PULL_DATA_TEMP_DIR;
  if (OsCreateDirectory(tmp_path, false) != 0) {
    ZSLOG_ERROR("Create tmp dir(%s) fail : %s", tmp_path.c_str(),
                zs::OsGetLastErr());
    error = ZISYNC_ERROR_OS_IO;
    return;
  }
  TmpDirDeleter tmpe_dir_deleter(tmp_path);

  if (OsAddHiddenAttr(tmp_path.c_str()) != 0) {
    ZSLOG_ERROR("OsAddHiddenAttr(%s) fail : %s", tmp_path.c_str(),
                OsGetLastErr());
    error = ZISYNC_ERROR_OS_IO;
    return;
  }

  unique_ptr<IGetTask> task(transfer_server->CreateGetTask(
          &monitor, local_tree.id(), "tar", remote_tree.uuid(),
          remote_device_data_uri));
  SyncGetHandler sync_get_handler(
      local_file_consistent_handler.get(), tmp_path, local_file_authority);
  task->SetHandler(&sync_get_handler);

  for (auto iter = data_tasks.begin(); iter != data_tasks.end(); iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    assert((*iter)->remote_file_stat() != NULL);
    err_t ret = task->AppendFile((*iter)->remote_file_stat()->path(),
                                 (*iter)->remote_file_stat()->length);
    if (ret != ZISYNC_SUCCESS) {
      error = ZISYNC_ERROR_GENERAL;
      continue;
    }
    sync_get_handler.AppendSyncFile(iter->release());
  }
  err_t ret = task->Execute(tmp_path);
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_INFO("Execute task fail : %s", zisync_strerror(ret));
    error = ZISYNC_ERROR_GENERAL;
  }
  // handle tail
  sync_get_handler.HandleGetFiles();

  if (sync_get_handler.error_code() != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Some Pull task handle fail");
    error = sync_get_handler.error_code();
  }

  zs::GetEventNotifier()->NotifyDownloadFileNumber(
      sync_get_handler.download_num());
  zs::OsDeleteDirectories(tmp_path);
}

void SyncFileTask::HandlePushTasks() {
  ITransferServer *transfer_server = GetTransferServer2();

  string meta_file = local_tree.root() + "/" + SYNC_FILE_TASKS_META_FILE;

  assert(push_sync_meta.mutable_remote_meta()->stats_size() == 0);
  MsgRemoteMeta *remote_meta = push_sync_meta.mutable_remote_meta();
  push_sync_meta.set_local_tree_uuid(remote_tree.uuid());
  push_sync_meta.set_remote_tree_uuid(local_tree.uuid());
  for (auto iter = local_tree_uuids->begin(); iter != local_tree_uuids->end();
       iter ++) {
    remote_meta->add_uuids(*iter);
  }
  for (auto iter = push_meta_tasks.begin(); iter != push_meta_tasks.end();
       iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    SetMsgStatByFileStat(
        remote_meta->add_stats(), *(*iter)->remote_file_stat());
  }

  vector<unique_ptr<SyncFile>> tasks;
  for (auto iter = push_data_tasks.begin(); iter != push_data_tasks.end();
       iter ++) {
    assert((*iter)->remote_file_stat() != NULL);
    if (SyncFile::IsFileStatConsistent(
            local_tree.root() + (*iter)->remote_file_stat()->path(), 
            (*iter)->remote_file_stat())) {
      tasks.emplace_back(iter->release());
    } else {
      ZSLOG_INFO("file(%s) is not consistent", 
                 (*iter)->remote_file_stat()->path());
    }
  }

  int32_t total_files = tasks.size();
  int64_t total_bytes = 0;
  for (auto iter = tasks.begin(); iter != tasks.end();
       iter ++) {
    total_bytes += (*iter)->remote_file_stat()->length;
  }

  TaskMonitor monitor(
      local_tree.id(), remote_tree.id(), ST_PUT, total_files, total_bytes);

  for (auto iter = tasks.begin(); iter != tasks.end(); iter++) {

    std::string file_name = (*iter)->remote_file_stat()->path();
    int64_t length = (*iter)->remote_file_stat()->length;
    std::string local_file_path = local_tree.root() + "/" + file_name;
    std::string remote_file_path = remote_tree.root() + "/" + file_name;

    monitor.AppendFile(
        local_file_path, remote_file_path, file_name, length);
  }
  unique_ptr<IPutTask> task(transfer_server->CreatePutTask(
          &monitor, local_tree.id(), "tar", remote_tree.uuid(),
          remote_device_data_uri));

  for (auto iter = tasks.begin(); iter != tasks.end(); iter ++) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree.id(), remote_tree.id())) {
      break;
    }
    SetMsgStatByFileStat(
        remote_meta->add_stats(), *(*iter)->remote_file_stat());
  }

  if (push_sync_meta.remote_meta().stats_size() == 0) {
    return;
  }
  string data = push_sync_meta.SerializeAsString();
  err_t ret = CreateFile(meta_file, data);
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Create Meta file fail");
    error = ZISYNC_ERROR_GENERAL;
    return;
  }

  PushSyncMetaDeleter deleter(meta_file);
  OsFileStat file_stat;
  if (OsStat(meta_file, string(), &file_stat) != 0) {
    ZSLOG_ERROR("OsStat Meta file fail");
    error = ZISYNC_ERROR_GENERAL;
    task.reset(transfer_server->CreatePutTask(
            &monitor, local_tree.id(), "tar", remote_tree.uuid(),
            remote_device_data_uri));
    return;
  }
  ret = task->AppendFile(meta_file, string("/") + SYNC_FILE_TASKS_META_FILE,
                         string(), file_stat.length); 
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Apend Meta file fail");
    error = ZISYNC_ERROR_GENERAL;
    return;
  }

  for (auto iter = tasks.begin(); iter != tasks.end(); iter ++) {
    ret = task->AppendFile(
        local_tree.root() + (*iter)->remote_file_stat()->path(), 
        (*iter)->remote_file_stat()->path(), 
        (*iter)->remote_file_stat()->alias,
        (*iter)->remote_file_stat()->length);
    if (ret != ZISYNC_SUCCESS) {
      ZSLOG_WARNING(
          "ApeendFile(%s) to PutTask fail : %s",
          (local_tree.root() + (*iter)->remote_file_stat()->path()).c_str(),
          zisync_strerror(ret));
      error = ZISYNC_ERROR_GENERAL;
      continue;
    }
  }

  ret = task->Execute();
  if (ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("PutTask execute fail : %s", zisync_strerror(ret));
    error = ZISYNC_ERROR_GENERAL;
    return;
  } 
}

int64_t SyncFileTask::num_byte_to_upload() { 
  int64_t byte = 0;
  for (auto iter = push_data_tasks.begin(); 
       iter != push_data_tasks.end(); iter ++) {
    assert((*iter)->remote_file_stat() != NULL);
    byte += (*iter)->remote_file_stat()->length;
  }
  return byte;
}

int64_t SyncFileTask::num_byte_to_download() { 
  int64_t byte = 0;
  for (auto iter = pull_data_tasks.begin(); 
       iter != pull_data_tasks.end(); iter ++) {
    assert((*iter)->remote_file_stat() != NULL);
    byte += (*iter)->remote_file_stat()->length;
  }
  return byte;
}



}  // namespace zs
