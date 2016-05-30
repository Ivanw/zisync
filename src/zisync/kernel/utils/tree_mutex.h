// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_TREE_MUTEX_H_
#define ZISYNC_KERNEL_UTILS_TREE_MUTEX_H_

#include <stdint.h>

#include <set>

namespace zs {

class Mutex;

class TreeMutex {
 public:
  static bool TryLock(int32_t local_tree_id, int32_t remote_tree_id);
  static void Unlock(int32_t local_tree_id, int32_t remote_tree_id);
  static void Clear();

 private:
  TreeMutex();
  TreeMutex(TreeMutex&);
  void operator=(TreeMutex&);

  static std::set<std::pair<int32_t /* local_tree_id */, 
      int32_t /* remote_tree_id */>> tree_mutex_set;
  static Mutex mutex;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_TREE_MUTEX_H_
