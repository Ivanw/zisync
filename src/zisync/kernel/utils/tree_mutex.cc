// Copyright 2014, zisync.com

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/tree_mutex.h"

namespace zs {

using std::pair;
using std::set;
using std::make_pair;

set<pair<int32_t, int32_t>> TreeMutex::tree_mutex_set;
Mutex TreeMutex::mutex;

bool TreeMutex::TryLock(int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto mutex_auto(&mutex);
  auto result = tree_mutex_set.insert(std::make_pair(
          local_tree_id, remote_tree_id));
  return result.second;
}

void TreeMutex::Unlock(int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto mutex_auto(&mutex);
  tree_mutex_set.erase(std::make_pair(local_tree_id, remote_tree_id));
}

void TreeMutex::Clear() {
  MutexAuto mutex_auto(&mutex);
  tree_mutex_set.clear();
}

}  // namespace zs

