// Copyright 2014, zisync.com

#include <memory>
#include <algorithm>

#include "zisync/kernel/platform/common.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/transfer.h"

namespace zs {

using std::unique_ptr;
using std::vector;

const char* Sync::full_projs[] = {
  TableSync::COLUMN_ID, TableSync::COLUMN_UUID, 
  TableSync::COLUMN_NAME, TableSync::COLUMN_LAST_SYNC,
  TableSync::COLUMN_STATUS, TableSync::COLUMN_TYPE,
  TableSync::COLUMN_DEVICE_ID, TableSync::COLUMN_PERM,
  TableSync::COLUMN_RESTORE_SHARE_PERM,
};

bool Sync::ExistsWhereStatusNormal(int32_t sync_id) {
  IContentResolver* resolver = GetContentResolver();
  const char *sync_projs[] = {
    TableSync::COLUMN_ID,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d AND %s = %d", TableSync::COLUMN_ID, sync_id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  return sync_cursor->MoveToNext();
}

err_t Sync::Create(const std::string &sync_name) {
  name_ = sync_name;

  IContentResolver* resolver = GetContentResolver();
  bool is_insert = true;

  if (uuid_.size() != 0) {
    const char* projections[] = {
      TableSync::COLUMN_ID, TableSync::COLUMN_STATUS,
    };
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableSync::URI, projections, ARRAY_SIZE(projections),
            "%s = '%s'", TableSync::COLUMN_UUID, uuid_.c_str()));
    if (cursor->MoveToNext()) {
      int32_t sync_status = cursor->GetInt32(1);
      id_ = cursor->GetInt32(0);
      if (sync_status == TableSync::STATUS_NORMAL) {
        ZSLOG_ERROR("Import an existing Sync(%s)", uuid_.c_str());
        return ZISYNC_ERROR_SYNC_EXIST;
      } else {
        is_insert = false;
      }
    }
  } else {
    OsGenUuid(&uuid_);
  }

  ContentValues sync_cv(4);
  sync_cv.Put(TableSync::COLUMN_NAME, name_);
  sync_cv.Put(TableSync::COLUMN_TYPE, type());
  sync_cv.Put(TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL);
  sync_cv.Put(TableSync::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID);
  sync_cv.Put(TableSync::COLUMN_PERM, SYNC_PERM_RDWR);
  sync_cv.Put(TableSync::COLUMN_RESTORE_SHARE_PERM, 
              TableSync::RESTORE_SHARE_PERM_NULL);
  if (is_insert) {
    sync_cv.Put(TableSync::COLUMN_UUID, uuid_.c_str());
    sync_cv.Put(TableSync::COLUMN_LAST_SYNC, TableSync::LAST_SYNC_NONE);
    int32_t row_id = resolver->Insert(TableSync::URI, &sync_cv, AOC_IGNORE);
    if (row_id < 0) {
      ZSLOG_ERROR("Insert new sync fail.");
      return ZISYNC_ERROR_CONTENT;
    }
    id_ = row_id;
  } else {
    int affected_row_num = resolver->Update(
        TableSync::URI, &sync_cv, "%s = %d", TableSync::COLUMN_ID, id_);
    if (affected_row_num != 1) {
      ZSLOG_ERROR("Insert existing sync fail.");
      return ZISYNC_ERROR_CONTENT;
    }
  }

  ZSLOG_INFO("Sync::Create(%s)", sync_name.c_str());
  
  return ZISYNC_SUCCESS;
}

err_t Sync::DeleteById(int32_t id) {
  IContentResolver* resolver = GetContentResolver();
  {
    const char* projections[] = {
      TableSync::COLUMN_STATUS, TableSync::COLUMN_TYPE,
    };
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableSync::URI, projections, ARRAY_SIZE(projections),
            "%s = %d", TableSync::COLUMN_ID, id));
    if (!cursor->MoveToNext() || 
        cursor->GetInt32(0) == TableSync::STATUS_REMOVE) {
      return ZISYNC_ERROR_SYNC_NOENT;
    }
  }

  Tree::DeleteBy(
      "%s = %d", TableTree::COLUMN_SYNC_ID, id);
  

  ContentValues share_sync_cv(1);
  share_sync_cv.Put(TableShareSync::COLUMN_SYNC_PERM, 
                    TableSync::PERM_DISCONNECT);
  resolver->Update(TableShareSync::URI, &share_sync_cv, "%s = %d",
                   TableShareSync::COLUMN_SYNC_ID, id);

  ContentValues cv(1);
  cv.Put(TableSync::COLUMN_STATUS, TableSync::STATUS_REMOVE);
  int num_affected_row = resolver->Update(
      TableSync::URI, &cv, "%s = %d", TableSync::COLUMN_ID, id);
  if (num_affected_row != 1) {
    ZSLOG_ERROR("Update Sync to remove fail.");
    return ZISYNC_ERROR_CONTENT;
  }


  return ZISYNC_SUCCESS;
}
  
int32_t Sync::GetIdByUuidWhereStatusNormal(const string &uuid) {
  IContentResolver* resolver = GetContentResolver();
  const char *sync_projs[] = {
    TableSync::COLUMN_ID,
  };

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = '%s' AND %s = %d",
          TableSync::COLUMN_UUID, uuid.c_str(), 
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (sync_cursor->MoveToNext()) {
    return sync_cursor->GetInt32(0);
  } else {
    return -1;
  }
}

Sync* Sync::GetByIdWhereStatusNormal(int32_t id) {
  IContentResolver* resolver = GetContentResolver();

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d",
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
		  TableSync::COLUMN_ID, id));
  if (sync_cursor->MoveToNext()) {
    return Generate(sync_cursor.get());
  } else {
    return NULL;
  }
}

Sync* Sync::GetByUuid(const std::string& uuid) {
  IContentResolver* resolver = GetContentResolver();

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
    TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
    " %s = '%s' ",TableSync::COLUMN_UUID, uuid.c_str()));
  if (sync_cursor->MoveToNext()) {
    return Generate(sync_cursor.get());
  } else {
    return NULL;
  }
}

Sync* Sync::GetBy(const char *selection, ...) {
  IContentResolver* resolver = GetContentResolver();
  va_list ap;
  va_start(ap, selection);
  unique_ptr<ICursor2> sync_cursor(resolver->vQuery(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          selection, ap));
  va_end(ap);
  if (sync_cursor->MoveToNext()) {
    return Generate(sync_cursor.get());
  } else {
    return NULL;
  }
}

Sync* Sync::GetByIdWhereStatusNormalTypeNotBackup(int32_t id) {
  IContentResolver* resolver = GetContentResolver();
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d AND %s != %d",
          TableSync::COLUMN_ID, id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  if (sync_cursor->MoveToNext()) {
    return Generate(sync_cursor.get());
  } else {
    return NULL;
  }
}

void Sync::QueryWhereStatusNormalTypeNotBackup(
    vector<unique_ptr<Sync>> *syncs) {
  IContentResolver* resolver = GetContentResolver();
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s != %d",
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  while (sync_cursor->MoveToNext()) {
    syncs->emplace_back(Generate(sync_cursor.get()));
  }
}

Sync* Sync::Generate(ICursor2 *cursor) {
  const int32_t sync_type = cursor->GetInt32(5);
  Sync *sync;
  if (sync_type == TableSync::TYPE_NORMAL) {
    sync = new NormalSync;
  } else if (sync_type == TableSync::TYPE_SHARED) {
    sync = new ShareSync;
  } else {
    sync = new Backup;
  }
  sync->ParseFromCursor(cursor);
  return sync;
}

void Sync::ParseFromCursor(ICursor2 *cursor) {
  id_ = cursor->GetInt32(0);
  uuid_ = cursor->GetString(1);
  name_ = cursor->GetString(2);
  last_sync_ = cursor->GetInt64(3);
  is_normal_ = cursor->GetInt32(4) == TableSync::STATUS_NORMAL;
  device_id_ = cursor->GetInt32(6);
  perm_ = cursor->GetInt32(7);
  restore_share_perm_ = cursor->GetInt32(8);
}

bool Sync::ToSyncInfo(SyncInfo *sync_info) const {
  sync_info->sync_id = id_;
  sync_info->sync_uuid = uuid_;
  sync_info->sync_name = name_;
  sync_info->last_sync = last_sync_;
  if (perm_ == TableSync::PERM_DISCONNECT) {
    sync_info->sync_perm = SYNC_PERM_DISCONNECT_UNRECOVERABLE;
  } else if (perm_ == TableSync::PERM_CREATOR_DELETE) {
    sync_info->sync_perm = SYNC_PERM_DISCONNECT_UNRECOVERABLE;
  } else if (perm_ == TableSync::PERM_TOKEN_DIFF) {
    sync_info->sync_perm = SYNC_PERM_DISCONNECT_RECOVERABLE;
  } else {
    sync_info->sync_perm = perm_;
  }
  unique_ptr<Device> device(Device::GetById(device_id_));
  if (!device) {
    return false;
  }
  sync_info->is_share = (type() == TableSync::TYPE_SHARED) &&
      !device->is_mine();
  device->ToDeviceInfo(&sync_info->creator);
  vector<unique_ptr<Tree>> trees;
  Tree::QueryBySyncIdWhereStatusNormal(id_, &trees);
  sync_info->trees.clear();
  for (auto iter = trees.begin(); iter != trees.end(); iter ++) {
    TreeInfo tree_info;
    err_t zisync_ret = (*iter)->ToTreeInfo(&tree_info);
    if (zisync_ret == ZISYNC_SUCCESS) {
      sync_info->trees.push_back(tree_info);
    }
  }

  if (device_id_ != TableDevice::LOCAL_DEVICE_ID) {
    return true;
  }

  class ShareSync {
   public:
    int32_t device_id, sync_perm;
  };

  vector<unique_ptr<ShareSync>> share_syncs;
  IContentResolver *resolver = GetContentResolver();
  const char *share_sync_projs[] = {
    TableShareSync::COLUMN_DEVICE_ID, TableShareSync::COLUMN_SYNC_PERM,
  };
  unique_ptr<ICursor2> share_sync_cursor(resolver->Query(
          TableShareSync::URI, share_sync_projs, ARRAY_SIZE(share_sync_projs),
          "%s = %d", TableShareSync::COLUMN_SYNC_ID, id_));
  while (share_sync_cursor->MoveToNext()) {
    ShareSync *share_sync = new ShareSync;
    share_sync->device_id = share_sync_cursor->GetInt32(0);
    share_sync->sync_perm = share_sync_cursor->GetInt32(1);
    assert(share_sync->sync_perm != TableSync::PERM_TOKEN_DIFF);
    if (share_sync->sync_perm == TableSync::PERM_DISCONNECT) {
      share_sync->sync_perm = SYNC_PERM_DISCONNECT_UNRECOVERABLE;
    }
    share_syncs.emplace_back(share_sync);
  }

  for (auto iter = share_syncs.begin(); iter != share_syncs.end(); iter ++) {
    unique_ptr<Device> device(Device::GetById((*iter)->device_id));
    if (!device || device->is_mine()) {
      continue;
    }
    ShareSyncInfo share_sync;
    device->ToDeviceInfo(&share_sync.device);
    share_sync.sync_perm = (*iter)->sync_perm;
    sync_info->share_syncs.push_back(share_sync);
  }

  return true;
}

void Sync::ToMsgSync(MsgSync *sync) const {
  sync->set_uuid(uuid_);
  sync->set_name(name_);
  sync->set_type(TableSyncTypeToMsg(type()));
  sync->set_is_normal(is_normal_);
  sync->set_perm(SyncPermToMsgSyncPerm(perm_));

  if (device_id_ != TableDevice::NULL_DEVICE_ID) {
    unique_ptr<Device> creator(Device::GetById(device_id_));
    if (creator) {
      MsgDevice *msg_creator = sync->mutable_creator();
      creator->ToMsgDevice(msg_creator);
    }
  }
}

err_t Backup::Create(const string &name, const string &root) {
  err_t zisync_ret = Sync::Create(name);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Sync::Create() fail : %s", zisync_strerror(zisync_ret));
    return zisync_ret;
  }

  BackupSrcTree tree;
  zisync_ret = tree.Create(id_, root);
  if (zisync_ret != ZISYNC_SUCCESS) {
    ZSLOG_ERROR("Tree::Create(%s) fail : %s", 
                root.c_str(), zisync_strerror(zisync_ret));
    Sync::DeleteById(id_);
    return zisync_ret;
  }
  IssueRefresh(tree.id());

  return ZISYNC_SUCCESS;
}

int32_t Backup::type() const {
  return TableSync::TYPE_BACKUP;
}

Backup* Backup::GetByIdWhereStatusNormal(int32_t id) {
  IContentResolver* resolver = GetContentResolver();
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d AND %s = %d",
          TableSync::COLUMN_ID, id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  if (sync_cursor->MoveToNext()) {
    Backup *backup = new Backup;
    backup->ParseFromCursor(sync_cursor.get());
    return backup;
  } else {
    return NULL;
  }
}

void Backup::QueryWhereStatusNormal(vector<unique_ptr<Backup>> *backups) {
  IContentResolver* resolver = GetContentResolver();

  /* @TODO maybe we can use view or join */

  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, full_projs, ARRAY_SIZE(full_projs),
          "%s = %d AND %s = %d",
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL,
          TableSync::COLUMN_TYPE, TableSync::TYPE_BACKUP));
  while (sync_cursor->MoveToNext()) {
    Backup *backup = new Backup;
    backup->ParseFromCursor(sync_cursor.get());
    backups->emplace_back(backup);
  }
}

bool Backup::ToBackupInfo(BackupInfo *backup_info) const {
  backup_info->backup_id = id_;
  backup_info->backup_name = name_;
  backup_info->last_sync = last_sync_;
    
  vector<unique_ptr<Tree>> trees;
  Tree::QueryBySyncIdWhereStatusNormal(id_, &trees);
  backup_info->target_trees.clear();
  bool has_src = false;
  for (auto iter = trees.begin(); iter != trees.end(); iter ++) {
    if ((*iter)->type() == TableTree::BACKUP_SRC) {
      (*iter)->ToTreeInfo(&backup_info->src_tree);
      has_src = true;
    } else {
      TreeInfo tree_info;
      (*iter)->ToTreeInfo(&tree_info);
      backup_info->target_trees.push_back(tree_info);
    }
  }
  return has_src;
}

int32_t NormalSync::type() const {
  return TableSync::TYPE_NORMAL;
}

int32_t ShareSync::type() const {
  return TableSync::TYPE_SHARED;
}

int32_t Sync::GetSyncPermByIdWhereStatusNormal(int32_t id) {
  IContentResolver *resolver = GetContentResolver();
  const char *sync_projs[] = {
    TableSync::COLUMN_PERM,
  };
  unique_ptr<ICursor2> sync_cursor(resolver->Query(
          TableSync::URI, sync_projs, ARRAY_SIZE(sync_projs),
          "%s = %d AND %s = %d",
          TableSync::COLUMN_ID, id,
          TableSync::COLUMN_STATUS, TableSync::STATUS_NORMAL));
  if (!sync_cursor->MoveToNext()) {
    // WR not allow push
    ZSLOG_ERROR("Noent Sync(%d)", id);
    return -1;
  }
  return sync_cursor->GetInt32(0);
}

bool Sync::IsUnusable(int32_t sync_perm) {
  return sync_perm == TableSync::PERM_DISCONNECT || 
      sync_perm == TableSync::PERM_TOKEN_DIFF ||
      sync_perm == TableSync::PERM_CREATOR_DELETE;
}

bool Sync::IsCreator(int32_t id)
{
	IContentResolver *resolver = GetContentResolver();
	const char* projections[] = {
		TableSync::COLUMN_DEVICE_ID,
	};
	unique_ptr<ICursor2> cursor(resolver->Query(
		TableSync::URI, projections, ARRAY_SIZE(projections),
		"%s = %d", TableSync::COLUMN_ID, id));

	if (cursor->MoveToNext()) {
		return cursor->GetInt32(0) == TableDevice::LOCAL_DEVICE_ID;
	}
  return false;
}

int32_t ToExternalSyncType(int32_t type) {
  if (type == TableSync::TYPE_NORMAL) {
    return SYNC_TYPE_NORMAL;
  }else if (type == TableSync::TYPE_BACKUP) {
    return SYNC_TYPE_BACKUP;
  }else if (type == TableSync::TYPE_SHARED) {
    return SYNC_TYPE_SHARED;
  }else {
    assert(false);
    return -1;
  }
}

}  // namespace zs
