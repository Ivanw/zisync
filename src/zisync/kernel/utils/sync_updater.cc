#include <cstring>
#include <memory>

#include "zisync/kernel/utils/sync_updater.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/worker/sync_file_task.h"
#include "zisync/kernel/status.h"
#include "zisync/kernel/zslog.h"

namespace zs {

using std::shared_ptr;

const bool has_remote_vclock = true;

SyncUpdater::SyncUpdater(int32_t local_tree_id, int32_t remote_tree_id):
    local_tree_id_(local_tree_id), remote_tree_id_(remote_tree_id) {}

bool SyncUpdater::Initialize() {
  assert(local_tree_id_ != remote_tree_id_);
  if (local_tree_id_ == remote_tree_id_) {
    ZSLOG_ERROR("local_tree_id(%d) = remote_tree_id",
                local_tree_id_);
    return false;
  }

  local_tree_.reset(Tree::GetByIdWhereStatusNormal(local_tree_id_));
  if (!local_tree_ || !local_tree_->IsLocalTree()) {
    ZSLOG_ERROR("Local Tree(%d) does not exist", local_tree_id_);
    return false;
  }
  local_tree_->FixRootForSync();

  remote_tree_.reset(Tree::GetByIdWhereStatusNormal(remote_tree_id_));
  if (!remote_tree_) {
    ZSLOG_ERROR("Tree(%d) does not exist", remote_tree_id_);
    return false;
  }

  if (local_tree_->sync_id() != remote_tree_->sync_id()) {
    ZSLOG_ERROR("sync_id(%d) of Tree(%d) != sync_id(%d) of Tree(%d)", 
                local_tree_->sync_id(), local_tree_->id(),
                remote_tree_->sync_id(), remote_tree_->id());
    return false;
  }

  sync_.reset(Sync::GetByIdWhereStatusNormal(local_tree_->sync_id()));
  if (!sync_ || sync_->perm() == TableSync::PERM_DISCONNECT ||
      sync_->perm() == TableSync::PERM_TOKEN_DIFF ||
      sync_->perm() == TableSync::PERM_CREATOR_DELETE) {
    ZSLOG_ERROR("Sync(%d) does not exist", local_tree_->sync_id());
    return false;
  }

  return true;
}

void SyncUpdater::Update(
    const Device *remote_device /* = NULL */, 
    const char *remote_device_ip /* = NULL */) {
  assert(local_tree_);
  assert(remote_tree_);
  assert(sync_);
  local_file_stats.clear();
  remote_file_stats.clear();
  remote_vclock_map_to_local.clear();
  sync_file_task_.reset(new SyncFileTask(
          *local_tree_, *remote_tree_, *sync_));
  SetSyncFileTask();
  if (remote_device != NULL) {
    sync_file_task_->set_remote_device(remote_device);
  }
  if (remote_device_ip != NULL) {
    sync_file_task_->set_remote_device_ip(remote_device_ip);
  }
  sync_file_task_->Prepare();
  UpdateTreePairStatus();
}

void SyncUpdater::SetSyncFileTask() {
  IContentResolver *resolver = GetContentResolver();
  local_tree_uuids.clear();
  
  bool has_local_tree_uuid_found = false, has_remote_tree_uuid_found = false;
  int32_t local_vclock_index_of_local_tree = -1,
          local_vclock_index_of_remote_tree = -1;
  {
    const char* tree_projs[] = {
      TableTree::COLUMN_UUID, 
    };
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
            "%s = %d", TableTree::COLUMN_SYNC_ID, sync_->id()));
    local_tree_uuids.push_back(local_tree_->uuid());
    while (cursor->MoveToNext()) {
      const char *tree_uuid = cursor->GetString(0);
      if (!has_local_tree_uuid_found && local_tree_->uuid() == tree_uuid) {
        has_local_tree_uuid_found = true;
        local_vclock_index_of_local_tree = local_tree_uuids.size() - 1;
      } else if (!has_remote_tree_uuid_found && remote_tree_->uuid() == tree_uuid) {
        has_remote_tree_uuid_found = true;
        if (has_local_tree_uuid_found) {
          local_vclock_index_of_remote_tree = local_tree_uuids.size();
        } else {
          local_vclock_index_of_remote_tree = local_tree_uuids.size() - 1;
        }
        local_tree_uuids.push_back(tree_uuid);
      } else {
        local_tree_uuids.push_back(tree_uuid);
      }
    }
  }
  
  sync_file_task_->set_local_tree_uuids(&local_tree_uuids);
#ifdef ZS_TEST
  if (local_tree_uuids.size() == 1) {
    ZSLOG_INFO("DatabaseInit in test");
    return;
  }
#endif
  assert(local_vclock_index_of_local_tree != -1);
  assert(local_vclock_index_of_remote_tree != -1);
  assert(local_vclock_index_of_local_tree != local_vclock_index_of_remote_tree);
  remote_vclock_map_to_local.resize(local_tree_uuids.size(), -1);

  if (local_vclock_index_of_local_tree < local_vclock_index_of_remote_tree) {
    remote_vclock_map_to_local[0] = local_vclock_index_of_remote_tree;
    for (int i = 1; i <= local_vclock_index_of_local_tree; i ++) {
      remote_vclock_map_to_local[i] = i;
    }
    remote_vclock_map_to_local[local_vclock_index_of_local_tree + 1] = 0;
    for (int i = local_vclock_index_of_local_tree + 2; 
         i <= local_vclock_index_of_remote_tree; i ++) {
      remote_vclock_map_to_local[i] = i - 1;
    }
    for (int i = local_vclock_index_of_remote_tree + 1; 
         i < static_cast<int32_t>(remote_vclock_map_to_local.size()); i ++) {
      remote_vclock_map_to_local[i] = i;
    }
  } else {
    remote_vclock_map_to_local[0] = local_vclock_index_of_remote_tree + 1;
    for (int i = 1; i <= local_vclock_index_of_remote_tree; i ++) {
      remote_vclock_map_to_local[i] = i;
    }
    for (int i = local_vclock_index_of_remote_tree + 1; 
         i < local_vclock_index_of_local_tree; i ++) {
      remote_vclock_map_to_local[i] = i + 1;
    }
    remote_vclock_map_to_local[local_vclock_index_of_local_tree] = 0;
    for (int i = local_vclock_index_of_local_tree + 1; 
         i < static_cast<int32_t>(remote_vclock_map_to_local.size()); i ++) {
      remote_vclock_map_to_local[i] = i;
    }
  }
  
  if (local_tree_->id() < remote_tree_->id()) {
    local_cursor.reset(resolver->sQuery(
            TableFile::GenUri(local_tree_->uuid().c_str()), 
            FileStat::file_projs_with_remote_vclock, 
            FileStat::file_projs_with_remote_vclock_len, 
            NULL, TableFile::COLUMN_PATH));
    remote_cursor.reset(resolver->sQuery(
            TableFile::GenUri(remote_tree_->uuid().c_str()), 
            FileStat::file_projs_with_remote_vclock, 
            FileStat::file_projs_with_remote_vclock_len, 
            NULL, TableFile::COLUMN_PATH));
  } else {
    remote_cursor.reset(resolver->sQuery(
            TableFile::GenUri(remote_tree_->uuid().c_str()), 
            FileStat::file_projs_with_remote_vclock, 
            FileStat::file_projs_with_remote_vclock_len, 
            NULL, TableFile::COLUMN_PATH));
    local_cursor.reset(resolver->sQuery(
            TableFile::GenUri(local_tree_->uuid().c_str()), 
            FileStat::file_projs_with_remote_vclock, 
            FileStat::file_projs_with_remote_vclock_len, 
            NULL, TableFile::COLUMN_PATH));
  }
  bool has_remote_vclock = true;
  
  if (local_cursor->MoveToNext()) {
    local_file_stat.reset(new FileStat(local_cursor.get(), has_remote_vclock));
  }
  if (remote_cursor->MoveToNext()) {
    remote_file_stat.reset(new FileStat(
            remote_cursor.get(), has_remote_vclock, 
            remote_vclock_map_to_local, local_tree_uuids.size()));
  }

  while ((!local_cursor->IsAfterLast()) || (!remote_cursor->IsAfterLast())) {
    if (zs::IsAborted() || zs::AbortHasSyncTreeDelete(
            local_tree_->id(), remote_tree_->id())) {
      break;
    }
    if (local_cursor->IsAfterLast()) {
      RemoteNewFile();
    } else if (remote_cursor->IsAfterLast()) {
      LocalNewFile();
    } else {
      int ret = strcmp(local_file_stat->path(), remote_file_stat->path()); 
      if (ret == 0) {
        UpdateFile();
      } else if (ret < 0) {
        LocalNewFile();
      } else {
        RemoteNewFile();
      }
    }
  }
  local_cursor.reset(NULL);
  remote_cursor.reset(NULL);
}

void SyncUpdater::RemoteNewFile() {
  if (SyncList::NeedSync(local_tree_->id(), remote_file_stat->path())) {
    assert(remote_file_stat.get() != NULL);
    bool need_sync = sync_file_task_->Add(NULL, remote_file_stat.get());
    if (need_sync) {
      remote_file_stats.emplace_back(remote_file_stat.release());
    }
  }
  if (remote_cursor->MoveToNext()) {
    remote_file_stat.reset(new FileStat(
            remote_cursor.get(), has_remote_vclock, 
            remote_vclock_map_to_local, local_tree_uuids.size()));
  }
}

void SyncUpdater::LocalNewFile() {
  if (SyncList::NeedSync(local_tree_->id(), local_file_stat->path())) {
    bool need_sync = sync_file_task_->Add(local_file_stat.get(), NULL);
    if (need_sync) {
      local_file_stats.emplace_back(local_file_stat.release());
    }
  }
  if (local_cursor->MoveToNext()) {
    local_file_stat.reset(new FileStat(
            local_cursor.get(), has_remote_vclock));
  }
}

void SyncUpdater::UpdateFile() {
  if (SyncList::NeedSync(local_tree_->id(), local_file_stat->path())) {
    sync_file_task_->Add(
        local_file_stat.get(), remote_file_stat.get());
    local_file_stats.emplace_back(local_file_stat.release());
    remote_file_stats.emplace_back(remote_file_stat.release());
    if (remote_cursor->MoveToNext()) {
      remote_file_stat.reset(new FileStat(
              remote_cursor.get(), has_remote_vclock, 
              remote_vclock_map_to_local, local_tree_uuids.size()));
    }
  }
  if (local_cursor->MoveToNext()) {
    local_file_stat.reset(new FileStat(
            local_cursor.get(), has_remote_vclock));
  }
}

void SyncUpdater::UpdateTreePairStatus() {
  shared_ptr<ITreePairStat> pair_stat = GetTreeManager()->
      GetTreePairStat(local_tree_->id(), remote_tree_->id());
  // ZSLOG_ERROR("fuck %d : %d : %d", 
  //             sync_file_task_->num_file_to_upload(),
  //             sync_file_task_->num_file_to_download(),
  //             sync_file_task_->num_file_consistent());
  // ZSLOG_ERROR("fuck %ld : %ld : %ld", 
  //             sync_file_task_->num_byte_to_upload(),
  //             sync_file_task_->num_byte_to_download(),
  //             sync_file_task_->num_byte_consistent());
  pair_stat->SetStaticFileToUpload(sync_file_task_->num_file_to_upload());
  pair_stat->SetStaticFileToDownload(sync_file_task_->num_file_to_download());
  pair_stat->SetStaticFileConsistent(sync_file_task_->num_file_consistent());
  pair_stat->SetStaticByteToUpload(sync_file_task_->num_byte_to_upload());
  pair_stat->SetStaticByteToDownload(sync_file_task_->num_byte_to_download());
  pair_stat->SetStaticByteConsistent(sync_file_task_->num_byte_consistent());
}


}  // namespace zs
