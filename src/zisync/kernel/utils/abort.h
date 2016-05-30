// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_ABORT_H_
#define ZISYNC_KERNEL_UTILS_ABORT_H_

#include <stdint.h>
#include <stdlib.h>

namespace zs {

/*  only call in main thread */
void AbortInit();
/*  only call in main thread */
void Abort();
bool IsAborted();
// void AbortDelSyncLocalTree(int32_t local_tree_id);
// void AbortAddSyncLocalTree(int32_t local_tree_id);
// void AbortDelSyncRemoteTree(int32_t remote_tree_id);
// void AbortAddSyncRemoteTree(int32_t remote_tree_id);
void AbortDelSyncTree(int32_t local_tree_id, int32_t remote_tree_id);
void AbortAddSyncTree(int32_t local_tree_id, int32_t remote_tree_id);
bool AbortHasSyncTreeDelete(int32_t local_tree_id, int32_t remote_tree_id);
// void AbortDelFsTree(int32_t tree_id);
// void AbortAddFsTree(int32_t tree_id);
bool AbortHasFsTreeDelete(int32_t tree_id);
void AbortClear();

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_ABORT_H_
