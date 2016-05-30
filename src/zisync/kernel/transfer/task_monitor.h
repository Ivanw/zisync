/**
 * @file transfer_monitor.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief default transfer monitor.
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

#ifndef ZISYNC_KERNEL_TRANSFER_TRANSFER_MONITOR_H_
#define ZISYNC_KERNEL_TRANSFER_TRANSFER_MONITOR_H_

#include <memory>

#include "zisync/kernel/libevent/transfer.h"
#include "zisync/kernel/status.h"

namespace zs {
using std::unique_ptr;

class TaskMonitor : public ITaskMonitor {
 public:
  TaskMonitor(int32_t lcoal_tree_id, 
              int32_t remote_tree_id,
              StatusType type,
              int32_t total_files,
              int64_t total_bytes);
  virtual ~TaskMonitor();

  virtual void OnFileTransfered(int32_t count);
  virtual void OnFileSkiped(int32_t count);

  virtual void OnByteTransfered(int64_t nbytes);
  virtual void OnByteSkiped(int64_t nbytes);

  virtual void OnFileTransfer(const string &path);

  virtual void AppendFile(const string &local_path,
                          const string &remote_path,
                          const string &encode_path,
                          int64_t length);
 private:
  int32_t total_file_;
  int32_t done_file_;
  int64_t total_byte_;
  int64_t done_byte_;

  StatusType type_;
  std::weak_ptr<ITreeStat> stat_;
  std::weak_ptr<ITreePairStat> pair_stat_;
  std::weak_ptr<ITransferList> transfer_list_stat_;
  int32_t sync_id_;
  int32_t sync_type_;
  int32_t local_tree_id_;
  int32_t remote_tree_id_;
  std::string active_path_;

  int64_t id_;

  static AtomicInt64 s_id_;
};

class IndexMonitor {
 public:
  IndexMonitor(int32_t tree_id, int32_t total_file_to_index);
  ~IndexMonitor();
  void OnFileWillIndex(const std::string file_indexing);
  void OnFileIndexed(int32_t count);

 private:
  int32_t total_file_to_index_;
  int32_t has_indexed_file_;

  std::weak_ptr<ITreeStat> stat_;
  int32_t tree_id_;
};

}  // namespace zs


#endif  // ZISYNC_KERNEL_TRANSFER_TRANSFER_MONITOR_H_
