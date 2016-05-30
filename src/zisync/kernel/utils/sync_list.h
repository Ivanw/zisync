// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_UTILS_SYNC_LIST_H_
#define ZISYNC_KERNEL_UTILS_SYNC_LIST_H_

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cassert>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

/* All function should be thread safe */

enum SyncListPathType {
  SYNC_LIST_PATH_TYPE_NULL      = 0,
  SYNC_LIST_PATH_TYPE_PARENT    = 1,
  SYNC_LIST_PATH_TYPE_CHILD     = 2,
  SYNC_LIST_PATH_TYPE_SELF      = 3,
  SYNC_LIST_PATH_TYPE_STRANGER  = 4,
};

enum SyncListType {
  WHITE_SYNC_LIST = 0,
  NULL_SYNC_LIST  = 1,
};

class Trie;
class SyncList {
 public :
  virtual ~SyncList();
  static void Clear();
  static void AddSyncList(int32_t tree_id, SyncListType sync_type);
  static err_t DelSyncList(int32_t tree_id);

  static err_t Insert(int32_t tree_id, const char *path);
  static err_t List(int32_t tree_id, std::vector<std::string> *paths);
  static err_t Remove(int32_t tree_id, const char *path);
  static SyncListPathType GetSyncListPathType(int32_t tree_id, const char *path);
  static bool NeedSync(int32_t tree_id, const char *path);

 protected:
  SyncList(const int32_t tree_id_);

  Trie *trie;
  int32_t tree_id;
 
 private:
  SyncList(const SyncList&);
  void operator=(const SyncList&);

  static std::map<int32_t, std::unique_ptr<SyncList>> sync_list_map;
  
  static SyncList* GetSyncList(int32_t tree_id);
  /**
   * @brief: Insert new entries into SyncList
   *
   * @param entries:
   */
  virtual err_t Insert(const char *path);
  virtual err_t List(std::vector<std::string> *paths);

  /**
   * @brief: remove entries from SyncList
   *
   * @param entries:
   */
  virtual err_t Remove(const char *path);
  virtual SyncListPathType GetSyncListPathType(const char *path);

  /**
   * @brief: determine a file or dir whether need sync
   *
   * @param path: the path of the file or dir
   *
   * @return:
   */
  virtual bool NeedSync(const char *path) = 0;
};

class WhiteSyncList : public SyncList {
  friend class SyncList;
 private:
  WhiteSyncList(const WhiteSyncList&);
  void operator=(const WhiteSyncList&);
  WhiteSyncList(const int32_t tree_id):SyncList(tree_id) {}
  /**
   * @brief: determine a file or dir whether is favorite
   *
   * @param path: the path of the file or dir
   *
   * @return:
   */
  virtual bool NeedSync(const char *path);
};

class NullSyncList : public SyncList {
  friend class SyncList;
 private:
  NullSyncList(const NullSyncList&);
  void operator=(const NullSyncList&);
  NullSyncList(const int32_t tree_id) : SyncList(tree_id) {}
  virtual bool NeedSync(const char *path) {
    return true;
  }
  virtual err_t Insert(const char *path) {
    assert(false);
    return ZISYNC_SUCCESS;
  }
  virtual err_t List(std::vector<std::string> *paths) {
    assert(false);
    return ZISYNC_SUCCESS;
  }
  virtual err_t Remove(const char *path) {
    assert(false);
    return ZISYNC_SUCCESS;
  }
  virtual SyncListPathType GetSyncListPathType(const char *path) {
    assert(false);
    return SYNC_LIST_PATH_TYPE_NULL;
  }
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_SYNC_LIST_H_
