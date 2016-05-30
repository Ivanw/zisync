// Copyright 2014, zisync.com

#ifndef ZISYNC_KERNEL_UTILS_TREE_PAIR_H_
#define ZISYNC_KERNEL_UTILS_TREE_PAIR_H_

#include <cstdint>
#include <set>

namespace zs {

class ITreeTraverseVisitor {
 public:
  virtual ~ITreeTraverseVisitor() {}
  virtual void TreePairVisit(
      int32_t local_tree_id, int32_t remote_tree_id) {}
  virtual void LocalTreeVisit(int32_t local_tree_id) {}
};

void TreeTraverse(ITreeTraverseVisitor *visitor);
}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_TREE_PAIR_H_
