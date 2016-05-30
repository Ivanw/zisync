/**
 * @file tar_get_task.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief tar format task implementation.
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

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_GET_TASK_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_GET_TASK_H_

#include <libtar.h>
#include <string>
#include <vector>

#include "zisync/kernel/transfer/transfer_server.h"
#include "zisync/kernel/transfer/transfer.pb.h"
#include "zisync/kernel/transfer/tar_get_task_common.h"

namespace zs {

class OsTcpSocket;

class TarGetTaskFactory : public GetTaskFactory {
  friend class TransferServer;
 public:
  virtual ~TarGetTaskFactory();

  virtual IGetTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx);
  const std::string& GetFormat();

 private:
  TarGetTaskFactory();

  std::string format_;
  static TarGetTaskFactory s_tar_get_task_factory_;
};

/**
 * Get Put a bounch of file (including data) of the same tree from remote.
 *
 * The file paths relative to tree root are sent as http body encoded by
 * protobuf message TarGetFileList defined in transfer.proto. that is,
 * GET tar HTTP/1.1\r\n
 * ZiSync-Remote-Tree-Uuid:00000000-0000-0000-0000-000000000000\r\n
 * ZiSync-Total-Size:total_size\r\n
 * ZiSync-Total-Files:total_files\r\n
 * \r\n
 * <path encoded by protobuf ...>
 *
 * The result of get is in libtar format, carried by http response. e.g. if succeed:
 *
 * HTTP/1.1 200 OK\r\n
 * \r\n
 * <data in libtar format ...>
 *
 * otherwise:
 *
 * HTTP/1.1 Status-Code Reason-Phrase\r\n
 * ZiSync-Error-Code: 123\r\n
 * ZiSync-Error-String: not suppored method\r\n
 * \r\n
 *
 * Note:
 *   1. boths the request and response SHOULD confirm to HTTP protocol.
 *   2. <data in libtar format ...> MUST be inflated by linux tar command
 *   3. <path encoded by protobuf ...> MUST be parser ok using google protobuf
 */
class TarGetTask : public IGetTask, TarGetTaskCommon {
  friend class TarGetTaskFactory;
 public:
  virtual ~TarGetTask();

  virtual int GetTaskId();

  virtual err_t AppendFile(
      const std::string& encode_path, int64_t size);

  virtual err_t AppendFile(
      const std::string& encode_path,
      const std::string& signature,
      int64_t size);

  virtual err_t Execute(const std::string& tmp_dir);
  virtual std::string GetTargetPath(const std::string &encode_path);
//  virtual bool PrepareFileParentDir(const std::string &path);
  virtual bool AllowPut(const std::string &path);
  virtual void SetHandler(IGetHandler *handler) {
    // @TODO implmentation
  }
  // virtual err_t SendGetHttpHeader(std::ostream *out);

 private:
  TarGetTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx);

  err_t SendRelativePathsProtobuf(std::ostream& out);

  std::string tmp_dir_;
  int32_t remote_tree_id_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TAR_GET_TASK_H_
