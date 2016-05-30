// Copyright 2014 zisync.com

#include <string>
#include <memory>
#include <cstring>
#include <vector>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sync_list.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/trie.h"

namespace zs {

using std::unique_ptr;
using std::vector;

static inline string fix_path(const char *path) {
  string fixed_path(path);
  if (strcmp(path, "/") == 0) {
    fixed_path = path;
  } else {
    fixed_path += "/";
  }
  return fixed_path;
}

static inline string restore_path(const string &path) {
  if (path != "/") {
    return path.substr(0, path.length() - 1);
  } else {
    return path;
  }
}

static Mutex sync_list_mutex; 
std::map<int32_t, std::unique_ptr<SyncList>> SyncList::sync_list_map;

void SyncList::Clear() {
  MutexAuto mutex(&sync_list_mutex);
  sync_list_map.clear();
}

SyncList::SyncList(const int32_t tree_id_):tree_id(tree_id_) {
  trie = new Trie();
  vector<string> paths;
  List(&paths);
  for (auto iter = paths.begin(); iter != paths.end(); iter ++) {
    string fixed_path = fix_path(iter->c_str());
    trie->Add(fixed_path);
  }
}

SyncList::~SyncList() {
  assert(trie != NULL);
  delete trie;
}

void SyncList::AddSyncList(
    int32_t tree_id, SyncListType sync_type) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list;
  if (sync_type == WHITE_SYNC_LIST) {
    sync_list = new WhiteSyncList(tree_id);
  } else {
    sync_list = new NullSyncList(tree_id);
  }
  sync_list_map[tree_id].reset(sync_list);
}

err_t SyncList::DelSyncList(int32_t tree_id) {
  IContentResolver *resolver = GetContentResolver();
  resolver->Delete(
      TableSyncList::URI, "%s = %d", TableSyncList::COLUMN_TREE_ID, tree_id);
  MutexAuto mutex(&sync_list_mutex);
  sync_list_map.erase(tree_id);
  return ZISYNC_SUCCESS;
}

err_t SyncList::Insert(int32_t tree_id, const char *path) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list = GetSyncList(tree_id);
  if (sync_list == NULL) {
    return ZISYNC_ERROR_TREE_NOENT;
  }

  return sync_list->Insert(path);
}
  
err_t SyncList::List(int32_t tree_id, std::vector<std::string> *paths) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list = GetSyncList(tree_id);
  if (sync_list == NULL) {
    return ZISYNC_ERROR_TREE_NOENT;
  }

  return sync_list->List(paths);
}
err_t SyncList::Remove(int32_t tree_id, const char *path) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list = GetSyncList(tree_id);
  if (sync_list == NULL) {
    return ZISYNC_ERROR_TREE_NOENT;
  }

  return sync_list->Remove(path);
}

SyncListPathType SyncList::GetSyncListPathType(
    int32_t tree_id, const char *path) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list = GetSyncList(tree_id);
  if (sync_list == NULL) {
    return SYNC_LIST_PATH_TYPE_STRANGER;
  }

  return sync_list->GetSyncListPathType(path);
}

bool SyncList::NeedSync(int32_t tree_id, const char *path) {
  MutexAuto mutex(&sync_list_mutex);
  SyncList *sync_list = GetSyncList(tree_id);
  if (sync_list == NULL) {
    return false;
  }

  return sync_list->NeedSync(path);
}

err_t SyncList::Insert(const char *path_) {
  // if WHERE path || '/' LIKE .path || '%' :
  //   return EEXIST
  // else:
  //   delete WHERE .path LIKE path || '/%'
  //   insert path
  const char *projs[] = {
    TableSyncList::COLUMN_PATH, 
  };
  // TODO can be transcation ?, this is not tread safe, becasue
  // database can change between query and change
  string fixed_path = fix_path(path_);
  const char *path = fixed_path.c_str();

  IContentResolver *resolver = GetContentResolver();
  {
    unique_ptr<ICursor2> cursor(resolver->Query(
            TableSyncList::URI, projs, ARRAY_SIZE(projs),
            "%s = %" PRId32 " AND '%s' || '/' LIKE %s || '%%'", 
            TableSyncList::COLUMN_TREE_ID, tree_id, 
            path, TableSyncList::COLUMN_PATH));
    if (cursor->MoveToNext()) {
      return ZISYNC_ERROR_SYNC_LIST_EXIST;
    }
  }

  resolver->Delete(
      TableSyncList::URI, "%s = %" PRId32 " AND %s LIKE '%s' || '%%'", 
      TableSyncList::COLUMN_TREE_ID, tree_id, 
      TableSyncList::COLUMN_PATH, path);
  ContentValues cv(2);
  cv.Put(TableSyncList::COLUMN_TREE_ID, tree_id);
  cv.Put(TableSyncList::COLUMN_PATH, path, true);
  resolver->Insert(TableSyncList::URI, &cv, AOC_IGNORE);

  bool ret = trie->Add(fixed_path);
  assert(ret);
  return ZISYNC_SUCCESS;
}

err_t SyncList::Remove(const char *path_) {
  // TODO can be transcation ?
  // if WHERE path || '/' = .path :
  //   remove path
  // else
  //   return NOENT
  IContentResolver *resolver = GetContentResolver();
  string fixed_path = fix_path(path_);
  const char *path = fixed_path.c_str();
  int affected_row_num = resolver->Delete(
      TableSyncList::URI, "%s = %" PRId32 " AND '%s' = %s", 
      TableSyncList::COLUMN_TREE_ID, tree_id,
      path, TableSyncList::COLUMN_PATH);

  if (affected_row_num == 0) {
    return ZISYNC_ERROR_SYNC_LIST_NOENT;
  } else {
    bool ret = trie->Del(fixed_path);
    assert(ret);
    return ZISYNC_SUCCESS;
  }
}

err_t SyncList::List(std::vector<std::string> *paths) {
  IContentResolver *resolver = GetContentResolver();
  const char *projs[] = {
    TableSyncList::COLUMN_PATH,
  };

  paths->clear();
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableSyncList::URI, projs, ARRAY_SIZE(projs),
          "%s = %" PRId32,
          TableSyncList::COLUMN_TREE_ID, tree_id));
  while (cursor->MoveToNext()) {
    paths->push_back(restore_path(cursor->GetString(0)));
  }
  return ZISYNC_SUCCESS;
}

SyncListPathType SyncList::GetSyncListPathType(const char *path_) {
  TrieSearchResultType type = trie->Find(fix_path(path_));
  switch (type) {
    case TRIE_SEARCH_RESULT_TYPE_PARENT:
      return SYNC_LIST_PATH_TYPE_PARENT;
    case TRIE_SEARCH_RESULT_TYPE_CHILD:
      return SYNC_LIST_PATH_TYPE_CHILD;
    case TRIE_SEARCH_RESULT_TYPE_SELF:
      return SYNC_LIST_PATH_TYPE_SELF;
    case TRIE_SEARCH_RESULT_TYPE_STRANGER:
      return SYNC_LIST_PATH_TYPE_STRANGER;
    default:
      ZSLOG_ERROR("Invalid type(%d)", type);
      assert(0);
  }
  assert(0);
  return SYNC_LIST_PATH_TYPE_CHILD;
}
  
SyncList* SyncList::GetSyncList(int32_t tree_id) {
  auto find = sync_list_map.find(tree_id);
  return find != sync_list_map.end() ? find->second.get() : NULL;
}

// the path who is in list or whose father is in list or whose
// child is in list
bool WhiteSyncList::NeedSync(const char *path) {
  // if HWERE path || '/' LIKE .path || '%' or
  // WHERE .path LIKE path || '/%'
  //   return true;
  // else false;
  TrieSearchResultType type = trie->Find(fix_path(path));
  assert(type != TRIE_SEARCH_RESULT_TYPE_NULL);
  return type != TRIE_SEARCH_RESULT_TYPE_STRANGER;
}

// bool BlackSyncList::NeedSync(const char *path) {
//   // if where path || '/' LIKE .path || '%' 
//   //   return false
//   IContentResolver *resolver = GetContentResolver();
//   const char *projs[] = {
//     TableSyncList::COLUMN_PATH, 
//   };
//   unique_ptr<ICursor2> cursor(resolver->Query(
//           TableSyncList::URI, projs, ARRAY_SIZE(projs),
//           "%s = %" PRId32 " AND '%s' || '/' LIKE %s || '%%' ",
//           TableSyncList::COLUMN_TREE_ID, tree_id,
//           path, TableSyncList::COLUMN_PATH));
//   return cursor->MoveToNext();
// }

}  // namespace zs

