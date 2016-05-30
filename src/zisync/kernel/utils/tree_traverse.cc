// Copyright 2014, zisync.com

#include <vector>
#include <memory>
#include <set>

#include "zisync/kernel/utils/tree_traverse.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"

namespace zs {

using std::vector;
using std::unique_ptr;

void TreeTraverse(ITreeTraverseVisitor *visitor) {
  const char* tree_projs[] = {
    TableTree::COLUMN_ID, 
    TableTree::COLUMN_SYNC_ID,
    TableTree::COLUMN_DEVICE_ID,
  };

  Selection selection("%s = %d AND %s = %d",
                      TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
                      TableTree::COLUMN_IS_ENABLED, 1);

  unique_ptr<ICursor2> tree_cursor(GetContentResolver()->sQuery(
      TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs), 
      &selection, TableTree::COLUMN_SYNC_ID)); 
  int32_t pre_sync_id = -1;
  vector<int32_t> local_tree_ids;
  vector<int32_t> tree_ids_;
  while(true) {
    int32_t sync_id = -1;
    bool tree_cursor_end = !tree_cursor->MoveToNext();
    if (tree_cursor_end || 
        ((sync_id = tree_cursor->GetInt32(1)) != pre_sync_id)) {
      for (auto local_iter = local_tree_ids.begin(); 
           local_iter != local_tree_ids.end(); local_iter ++) {
        for (auto remote_iter = tree_ids_.begin(); 
             remote_iter != tree_ids_.end(); remote_iter ++) {
          if (*local_iter != *remote_iter) {
            visitor->TreePairVisit(*local_iter, *remote_iter);
          }
        }
      }
      local_tree_ids.clear();
      tree_ids_.clear();
    }
    if (tree_cursor_end) {
      break;
    }
    int32_t tree_id = tree_cursor->GetInt32(0);
    int32_t is_local = 
        tree_cursor->GetInt32(2) == TableDevice::LOCAL_DEVICE_ID;
    if (is_local) {
      // AbortAddFsTree(tree_id);
      visitor->LocalTreeVisit(tree_id);
      local_tree_ids.push_back(tree_id);
    }//todo: why no else
    tree_ids_.push_back(tree_id);
    pre_sync_id = sync_id;
  }
}

}  // namespace zs
