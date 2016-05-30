/**
 * @file status.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief status statistics for each tree.
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

#ifndef ZISYNC_KERNEL_STATUS_H_
#define ZISYNC_KERNEL_STATUS_H_

#include "zisync_kernel.h"
#include <vector>
#include <string>
#include <map>
#include <memory>

#include "zisync/kernel/platform/platform.h"

namespace zs {

class TreeStatusManager;

enum StatusType {
  ST_PUT, ST_GET, ST_INDEX, ST_WAIT_PUT, ST_WAIT_GET,
};

class ITreeStat {
 public:
  virtual ~ITreeStat() { }

  virtual void OnFileIndexed(int32_t count) = 0;
  virtual void OnFileTransfered(StatusType type, int32_t count) = 0;
  virtual void OnFileSkiped(StatusType type, int32_t count) = 0;
  virtual void OnByteTransfered(StatusType type, int64_t nbytes) = 0;
  virtual void OnByteSkiped(StatusType type, int64_t nbytes) = 0;
  virtual void OnTreeStatBegin(
      StatusType type, int32_t total_files, int64_t total_bytes) = 0;
  virtual void OnTreeStatEnd(
      StatusType type, int32_t total_files, int64_t total_bytes) = 0;
  virtual void OnFileWillIndex(const std::string &being_indexed) = 0;
};

class ITransferList {
 public:
  virtual ~ITransferList() { }

  virtual void OnFileTransfered(StatusType type,
                                int64_t id,
                                const std::string &encode_path) = 0;
  virtual void OnFileSkiped(StatusType type,
                            int64_t id,
                            const std::string &encode_path) = 0;
  virtual void OnByteTransfered(StatusType type,
                                int64_t nbytes,
                                int64_t id,
                                const std::string &encode_path) = 0;
  virtual void OnByteSkiped(StatusType type,
                            int64_t nbytes,
                            int64_t id,
                            const std::string &encode_path) = 0;
  virtual void AppendFile(const std::string &local_path,
                          const std::string &remote_path,
                          const std::string &encode_path,
                          int64_t length,
                          int64_t id) = 0;
  virtual void OnTransferListBegin(StatusType type,
                                   int64_t id) = 0;
  virtual void OnTransferListEnd(StatusType type,
                                 int64_t id) = 0;
};

class ITreePairStat {
 public:
  virtual ~ITreePairStat() {}
  virtual void OnFileTransfered(StatusType type, int32_t count) = 0;
  virtual void OnFileSkiped(StatusType type, int32_t count) = 0;
  virtual void OnFileTransfer(StatusType type, const string &path) = 0;
  virtual void OnByteTransfered(StatusType type, int64_t nbytes) = 0;
  virtual void OnByteSkiped(StatusType type, int64_t nbytes) = 0;
  virtual void OnTreeStatBegin(
      StatusType type, int32_t total_files, int64_t total_bytes) = 0;
  virtual void OnTreeStatEnd(
      StatusType type, int32_t total_files, int64_t total_bytes) = 0;
  virtual void SetStaticFileToUpload(int32_t count) = 0;
  virtual void SetStaticFileToDownload(int32_t count) = 0;
  virtual void SetStaticFileConsistent(int32_t count) = 0;
  virtual void SetStaticByteToUpload(int64_t bytes) = 0;
  virtual void SetStaticByteToDownload(int64_t bytes) = 0;
  virtual void SetStaticByteConsistent(int64_t bytes) = 0;
  void QueryTreePairStatus(TreePairStatus* status);
};

class ITreeManager {
  friend ITreeManager* GetTreeManager();

 public:
  virtual ~ITreeManager()  { }

  virtual err_t StartupTimer()  = 0;
  virtual err_t ShutdownTimer() = 0;

  virtual void OnTaskBegin(
      int32_t local_tree_id, int32_t remote_tree_id,
      StatusType type, int32_t total_files,
      int64_t total_bytes, int64_t id = -1) = 0;
  virtual void OnTaskEnd(
      int32_t local_tree_id, int32_t remote_tree_id, StatusType type,
      int32_t left_files, int64_t left_bytes, int64_t id = -1) = 0;
  virtual std::shared_ptr<ITreeStat> GetTreeStat(int32_t tree_id) = 0;
  virtual std::shared_ptr<ITreePairStat> GetTreePairStat(
      int32_t local_tree_id, int32_t remote_tree_id) = 0;
  virtual std::shared_ptr<ITransferList> GetTransferListStat(
      int32_t tree_id) = 0;

  virtual err_t QueryTreeStatus(int32_t tree_id, TreeStatus* status) = 0;
  virtual err_t QueryTreePairStatus(
      int32_t local_tree_id, int32_t remote_tree_id, 
      TreePairStatus* status) = 0;
  virtual err_t QueryTransferList(
      int32_t tree_id, TransferListStatus *list,
      int32_t offset, int32_t max_num) = 0;
};

ITreeManager* GetTreeManager();
}  // namespace zs

#endif  // ZISYNC_KERNEL_STATUS_H_
