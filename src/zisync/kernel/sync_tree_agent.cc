// Copyright 2014, zisync.com

#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/sync_tree_agent.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/platform/common.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/utils/tree_mutex.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/sync_put_handler.h"
#include "zisync/kernel/utils/sync_upload_handler.h"
#include "zisync/kernel/tree_status.h"
#include "zisync/kernel/transfer/task_monitor.h"

namespace zs{

using std::string;
using std::unique_ptr;

string SyncTreeAgent::GetTreeRoot(const string& tree_uuid) {
  IContentResolver *resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_ROOT,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s'", TableTree::COLUMN_UUID, tree_uuid.c_str()));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Nonexistent tree(%s)", tree_uuid.c_str());
    return string();
  } else {
    return string(tree_cursor->GetString(0));
  }
}

string SyncTreeAgent::GetNewTmpDir(const string &tree_uuid) {
  string tree_root = GetTreeRoot(tree_uuid);
  if (tree_root.length() == 0) {
    ZSLOG_ERROR("Nonexistent tree(%s)", tree_uuid.c_str());
    return tree_root;
  }

  string tmp_path;
  if (!zs::OsTempPath(tree_root, PULL_DATA_TEMP_DIR, &tmp_path)) {
    return string();
  }

  return tmp_path;
}

int32_t SyncTreeAgent::GetTreeId(const string& tree_uuid) {
  IContentResolver *resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s'", TableTree::COLUMN_UUID, tree_uuid.c_str()));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Nonexistent tree(%s)", tree_uuid.c_str());
    return -1;
  }
  return tree_cursor->GetInt32(0);
}

int32_t SyncTreeAgent::GetSyncId(const string& tree_uuid) {
  IContentResolver* resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_SYNC_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%s'", TableTree::COLUMN_UUID, tree_uuid.c_str()));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Nonexistent tree(%s)", tree_uuid.c_str());
    return -1;
  }
  return tree_cursor->GetInt32(0);
}

string SyncTreeAgent::GetTreeUuid(const int32_t tree_id) {
  IContentResolver* resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_UUID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%d'", TableTree::COLUMN_ID, tree_id));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Nonexistent tree(%d)", tree_id);
    return string();
  }
  return tree_cursor->GetString(0);
}

bool SyncTreeAgent::AllowPut(
    int32_t local_tree_id, int32_t remote_tree_id, 
    const std::string& relative_path) {
  assert(local_tree_id != NULL_TREE_ID);
  if (remote_tree_id == NULL_TREE_ID) {
    // for download only has local tree_id, check the tree still exist
    return !zs::AbortHasFsTreeDelete(local_tree_id);
  }
  if (AbortHasSyncTreeDelete(local_tree_id, remote_tree_id)) {
    return false;
  }
  if (strcmp(&(*relative_path.begin()) + 1, SYNC_FILE_TASKS_META_FILE) 
      == 0) {
    return true;
  } else {
    return SyncList::NeedSync(local_tree_id, relative_path.c_str());
  }
}

bool SyncTreeAgent::TryLock(int32_t local_tree_id, int32_t remote_tree_id) {
  return TreeMutex::TryLock(local_tree_id, remote_tree_id);
}

void SyncTreeAgent::Unlock(int32_t local_tree_id, int32_t remote_tree_id) {
  return TreeMutex::Unlock(local_tree_id, remote_tree_id);
}

IPutHandler* SyncTreeAgent::CreatePutHandler(const string &tmp_root) {
  return new SyncPutHandler(tmp_root);
}

IPutHandler* SyncTreeAgent::CreateUploadHandler(
    const string &tree_uuid, const string &tmp_root) {
  return new SyncUploadHandler(tree_uuid, tmp_root);
}

ITaskMonitor* SyncTreeAgent::CreateTaskMonitor(
    TaskType type,
    const std::string& local_tree_uuid,
    const std::string& remote_tree_uuid,
    int32_t total_files, int64_t total_bytes) {
  StatusType st = (type == TASK_TYPE_PUT) ? ST_PUT : ST_GET;
  int32_t local_tree_id = GetTreeId(local_tree_uuid);
  int32_t remote_tree_id = GetTreeId(remote_tree_uuid);
  
  return new TaskMonitor(
      local_tree_id, remote_tree_id, st, total_files, total_bytes);
}

string SyncTreeAgent::GetAlias(
    const string &tree_uuid, const string &tree_root, const string &relative_path) {
  if (tree_root != "/asserts-library") {
    return string();
  }
  IContentResolver *resolver = GetContentResolver();
  const char *file_projs[] = { TableFile::COLUMN_ALIAS, };
  unique_ptr<ICursor2> file_cursor(resolver->Query(
          TableFile::GenUri(tree_uuid.c_str()), 
          file_projs, ARRAY_SIZE(file_projs),
          "%s = '%s' AND %s = %d", TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(relative_path).c_str(),
          TableFile::COLUMN_STATUS, TableFile::STATUS_NORMAL));
  if (!file_cursor->MoveToNext()) {
    return string();
  }
  return file_cursor->GetString(0);
}

SyncTreeAgent SyncTreeAgent::sync_tree_agent;

}  // namespace
