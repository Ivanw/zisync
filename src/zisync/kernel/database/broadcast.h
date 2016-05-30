/**
 * @file broadcast.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Broadcast service implmentation.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#ifndef ZISYNC_KERNEL_DATABASE_BROADCAST_H_
#define ZISYNC_KERNEL_DATABASE_BROADCAST_H_

#include <vector>
#include <unordered_map>

namespace zs {
/**
 * Broadcast message for BM_TOINDEX, BM_INDEXED
 */
class BroadcastMessageIndex : public IBroadcastMessageIndex {
 public:
  BroadcastMessageIndex(int32_t tree_id, int32_t count) {
    tree_id_ = tree_id;
    count_ = count;
  }

  int32_t GetTreeId() {
    return tree_id_;
  }

  int32_t GetCount() {
    return count_;
  }

 protected:
  int32_t tree_id_;
  int32_t count_;
};

class BroadcastMessageUpdate : public IBroadcastMessageUpdate{
 public:
  BroadcastMessageUpdate(int32_t tree_id, int32_t sync_id, int32_t count) {
    tree_id_ = tree_id;
    sync_id_ = sync_id;
    count_ = count;
  }

  int32_t GetSyncId() {
    return sync_id_;
  }

  int32_t GetTreeId() {
    return tree_id_;
  }

  int32_t GetCount() {
    return count_;
  }

 protected:
  int32_t tree_id_;
  int32_t sync_id_;
  int32_t count_;
};

class BroadcastMessageTransfer : public IBroadcastMessageTransfer {
 public:
  BroadcastMessageTransfer(int32_t tree_id, int32_t sync_id, int64_t nbytes) {
    tree_id_ = tree_id;
    sync_id_ = sync_id;
    nbytes_ = nbytes;
  }

  int32_t GetTreeId() {
    return tree_id_;
  }

  int32_t GetSyncId() {
    return sync_id_;
  }

  int64_t GetBytes() {
    return nbytes_;
  }

 protected:
  int32_t tree_id_;
  int32_t sync_id_;
  int64_t nbytes_;
};

class OsMutex;

class BroadcastMessageHasher{
 public :
  size_t operator()(const BroadcastMessage& m) const {
    return m;
  }
};

class BroadcastService : public IBroadcastService {
  BroadcastService();
  virtual ~BroadcastService();

 public:
  virtual bool RegisterReceiver(
      BroadcastMessage msg, BroadcastReceiver* lpReceiver);
  virtual bool UnregisterReceiver(
      BroadcastMessage msg, BroadcastReceiver* lpReceiver);

  virtual bool PublishBroadcast(BroadcastMessage msg, void* lParam = NULL);


  static BroadcastService s_hInstance;
 protected:
  OsMutex* mutex_;
  std::unordered_map<BroadcastMessage,
                     std::vector<BroadcastReceiver*>,
                     BroadcastMessageHasher> receiver_hashtable_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_BROADCAST_H_
