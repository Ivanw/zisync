// Copyright 2014, zisync.com
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <iterator>
#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/tree_traverse.h"
#include "zisync/kernel/libevent/transfer.h"

namespace zs {

using std::set;
using std::pair;
using std::vector;
using std::map;
using std::unique_ptr;

class TreeChangeObserver : public ContentObserver{
 public:
  virtual ~TreeChangeObserver() {}
  //
  // Implement ContentObserver
  virtual void* OnQueryChange() { return NULL; }
  virtual void  OnHandleChange(void* lpChanges);
};

static bool is_aborted = false;
static set<int32_t> fs_tree_ids; // used for refresh and monitor
// static map<int32_t /* local_tree_id */, 
//     set<int32_t /* remote_tree_id */>> sync_tree_ids; 
static set<int64_t> sync_tree_ids;
static Mutex sync_tree_mutex;
static Mutex fs_tree_mutex;
static TreeChangeObserver tree_change_observer;

#define make_int64(local_id, remote_id) \
    (((int64_t)local_id << 32) + ((int64_t)remote_id))
#define parse_local_tree_id(i64) ((int32_t)(((i64) >> 32) & 0xFFFFFFFF))
#define parse_remote_tree_id(i64) ((int32_t)((i64) & 0xFFFFFFFF))

void AbortInit() {
  is_aborted = false;

  tree_change_observer.OnHandleChange(NULL);

  GetContentResolver()->RegisterContentObserver(
      TableTree::URI, false, &tree_change_observer);
}

void AbortClear() {
  GetContentResolver()->UnregisterContentObserver(
      TableTree::URI, &tree_change_observer);

  MutexAuto mutex_sync(&sync_tree_mutex);
  MutexAuto mutex_fs(&fs_tree_mutex);
  sync_tree_ids.clear();
  fs_tree_ids.clear();
}

void Abort() {
  is_aborted = true;
}

bool IsAborted() {
  return is_aborted;
}

void AbortDelSyncLocalTree(int32_t local_tree_id) {
  MutexAuto mutex(&sync_tree_mutex);
  sync_tree_ids.erase(local_tree_id);
}

void AbortAddSyncLocalTree(int32_t local_tree_id) {
  // Don't need this anymore

  // MutexAuto mutex(&sync_tree_mutex);
  // IContentResolver* resolver = GetContentResolver();
  // 
  // int32_t sync_id = Tree::GetSyncIdByIdWhereStatusNormal(local_tree_id);
  // if (sync_id == -1) {
  //   return;
  // }
  // 
  // set<int32_t> &find = sync_tree_ids[local_tree_id];
  // const char* tree_projs[] = {
  //   TableTree::COLUMN_ID, TableTree::COLUMN_DEVICE_ID,
  // };
  // 
  // unique_ptr<ICursor2> tree_cursor(resolver->Query(
  //         TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
  //         "%s = %d AND %s != %d AND %s = %d", 
  //         TableTree::COLUMN_SYNC_ID, sync_id,
  //         TableTree::COLUMN_ID, local_tree_id,
  //         TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  // while(tree_cursor->MoveToNext()) {
  //   int32_t remote_tree_id = tree_cursor->GetInt32(0);
  //   find.insert(remote_tree_id);
  //   bool is_local = tree_cursor->GetInt32(1) == TableDevice::LOCAL_DEVICE_ID;
  //   if (is_local) {
  //     sync_tree_ids[remote_tree_id].insert(local_tree_id);
  //   }
  // }
}

void AbortDelSyncRemoteTree(int32_t remote_tree_id) {
}

void AbortAddSyncRemoteTree(int32_t remote_tree_id) {
  // Dont need this anymore

  // MutexAuto mutex(&sync_tree_mutex);
  // IContentResolver* resolver = GetContentResolver();
  // 
  // int32_t sync_id = Tree::GetSyncIdByIdWhereStatusNormal(remote_tree_id);
  // if (sync_id == -1) {
  //   return;
  // }
  // 
  // const char* tree_projs[] = {
  //   TableTree::COLUMN_ID, 
  // };
  // 
  // unique_ptr<ICursor2> tree_cursor(resolver->Query(
  //         TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
  //         "%s = %d AND %s != %d AND %s = %d AND %s = %d", 
  //         TableTree::COLUMN_SYNC_ID, sync_id,
  //         TableTree::COLUMN_ID, remote_tree_id,
  //         TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID,
  //         TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL));
  // while(tree_cursor->MoveToNext()) {
  //   int32_t local_tree_id = tree_cursor->GetInt32(0);
  //   sync_tree_ids[local_tree_id].insert(remote_tree_id);
  // }
}

void AbortDelSyncTree(int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto mutex(&sync_tree_mutex);
  sync_tree_ids.erase(make_int64(local_tree_id, remote_tree_id));
}

void AbortAddSyncTree(int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto mutex(&sync_tree_mutex);
  // sync_tree_ids[local_tree_id].insert(remote_tree_id);
  sync_tree_ids.insert(make_int64(local_tree_id, remote_tree_id));
}

bool AbortHasSyncTreeDelete(int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto mutex(&sync_tree_mutex);
  return sync_tree_ids.find(make_int64(local_tree_id, remote_tree_id)) == sync_tree_ids.end();
}

void AbortDelFsTree(int32_t tree_id) {
  MutexAuto mutex(&fs_tree_mutex);
  fs_tree_ids.erase(tree_id);
}

void AbortAddFsTree(int32_t tree_id) {
  MutexAuto mutex(&fs_tree_mutex);
  fs_tree_ids.insert(tree_id);
}

bool AbortHasFsTreeDelete(int32_t tree_id) {
  MutexAuto mutex(&fs_tree_mutex);
  return fs_tree_ids.find(tree_id) == fs_tree_ids.end();
}

void TreeChangeObserver::OnHandleChange( void* lpChanges )
{
  unique_ptr<ICursor2> tree_cursor((ICursor2*)lpChanges);
  ZSLOG_INFO("Tree table changed, reload from SQLite");

  set<int64_t> tmp_sync_set;
  set<int32_t> tmp_fs_set;

  class TreeChangeTraverseVisitor : public ITreeTraverseVisitor {
   public:
    TreeChangeTraverseVisitor(
        set<int64_t> *tmp_sync_set, set<int32_t> *tmp_fs_set):
        tmp_sync_set_(tmp_sync_set), tmp_fs_set_(tmp_fs_set) {}
    virtual ~TreeChangeTraverseVisitor() {}
    virtual void TreePairVisit(
        int32_t local_tree_id, int32_t remote_tree_id) {
      tmp_sync_set_->insert(make_int64(local_tree_id, remote_tree_id));
    }
    virtual void LocalTreeVisit(int32_t local_tree_id) {
      tmp_fs_set_->insert(local_tree_id);
    }
   private:
    set<int64_t> *tmp_sync_set_;
    set<int32_t> *tmp_fs_set_;
  };

  TreeChangeTraverseVisitor visitor(&tmp_sync_set, &tmp_fs_set);
  TreeTraverse(&visitor);

  set<int64_t> new_sync_set;
  // set<int64_t> del_sync_set;

  { 
    MutexAuto autoit(&sync_tree_mutex);
    set_difference(
        tmp_sync_set.begin(), tmp_sync_set.end(), 
        sync_tree_ids.begin(), sync_tree_ids.end(),
        inserter(new_sync_set, new_sync_set.end()));

    // set_difference(
    //   sync_tree_ids.begin(), sync_tree_ids.end(),
    //   tmp_sync_set.begin(), tmp_sync_set.end(), 
    //   inserter(del_sync_set, del_sync_set.end()));

    sync_tree_ids.swap(tmp_sync_set);
  }

  // notify new sync pair to sync once
  for (auto it = new_sync_set.begin(); it != new_sync_set.end(); it++) {
    int32_t local_tree_id = parse_local_tree_id(*it);
    int32_t remote_tree_id = parse_remote_tree_id(*it);
    IssueSync(local_tree_id, remote_tree_id);
  }

  set<int32_t> new_fs_set;
  set<int32_t> del_fs_set;

  {
    MutexAuto autoit(&fs_tree_mutex);
    set_difference(
        tmp_fs_set.begin(), tmp_fs_set.end(),
        fs_tree_ids.begin(), fs_tree_ids.end(),
        inserter(new_fs_set, new_fs_set.end()));

    set_difference(
      fs_tree_ids.begin(), fs_tree_ids.end(),
      tmp_fs_set.begin(), tmp_fs_set.end(),
      inserter(del_fs_set, del_fs_set.end()));

    fs_tree_ids.swap(tmp_fs_set);
  }

  // notify new fs dir to refresh
  for (auto it = new_fs_set.begin(); it != new_fs_set.end(); it++) {
    IssueRefresh(*it);
  }

  IContentResolver *resolver = GetContentResolver();
  const char *tree_projs[] = { TableTree::COLUMN_UUID };
  for (auto it = del_fs_set.begin(); it != del_fs_set.end(); it++) {
    int32_t tree_id = *it;
    unique_ptr<ICursor2> tree_cursor(resolver->Query(
            TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
            "%s = %d AND %s = %d", 
            TableTree::COLUMN_ID, tree_id,
            TableTree::COLUMN_STATUS, TableTree::STATUS_REMOVE));
    if (tree_cursor->MoveToNext()) {
      const char *tree_uuid = tree_cursor->GetString(0);
      if (!resolver->DelProvider(
              TableFile::GenAuthority(tree_uuid).c_str(),
              true)) {
        ZSLOG_WARNING("DelTreeProvider(%s) fail", tree_uuid);
      }
    }
    zs::GetTransferServer2()->CancelTaskByTree(tree_id);
  }
}

}  // namespace zs
