/**
 * @file tar_put_task.h
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

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_PUT_TASK_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_PUT_TASK_H_

#include <libtar.h>
#include <string>
#include <vector>

#include "zisync/kernel/transfer/transfer_server.h"

namespace zs {

class OsTcpSocket;

class TarPutTaskFactory : public PutTaskFactory {
  friend class TransferServer;
 public:
  virtual ~TarPutTaskFactory();

  virtual IPutTask* CreateTask(
      ITaskMonitor* monitor,
      const int32_t local_tree_id,
      const std::string& remote_tree_uuid,
      const std::string& uri,
      SSL_CTX* ctx);
  virtual const std::string& GetFormat();

 private:
  TarPutTaskFactory();

  std::string format_;
  static TarPutTaskFactory s_tar_put_task_factory_;
};

/**
 * Send Put a bounch of file (including data) of the same tree to remote.
 *
 * The files are send as a tar stream carried by http body. that is the
 * socket stream has the following format:
 *
 * PUT tar HTTP/1.1\r\n
 * ZiSync-Remote-Tree-Uuid:00000000-0000-0000-0000-000000000000\r\n
 * ZiSync-Local-Tree-Uuid:00000000-0000-0000-0000-000000000000\r\n
 * ZiSync-Total-Size:total_size\r\n
 * ZiSync-Total-Files:total_files\r\n
 * \r\n
 * <data in libtar format ...>
 *
 * The put-processing result is send as http response. e.g. if succeed:
 *
 * HTTP/1.1 200 OK\r\n
 * \r\n
 *
 * otherwise:
 *
 * HTTP/1.1 Status-Code Reason-Phrase\r\n
 * \r\n
 *
 * Note:
 *   1. boths the request and response SHOULD confirm to HTTP protocol.
 *   2. the <data in libtar format ...> MUST be inflated by linux tar command
 */
class TarPutTask : public IPutTask {
  friend class TarPutTaskFactory;
 public:
  virtual ~TarPutTask();

  virtual int GetTaskId();

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path,
      int64_t size);

  virtual err_t AppendFile(
      const std::string& real_path,
      const std::string& encode_path,
      const std::string& signature,
      int64_t size);

  virtual err_t Execute();

 private:
  TarPutTask();
  TarPutTask(ITaskMonitor* monitor,
             const int32_t local_tree_id,
             const std::string& remote_tree_uuid,
             const std::string& uri,
             SSL_CTX* ctx);
  err_t SendPutHttpHeader(std::ostream& sock);

  std::vector<std::string> real_path_vector_;
  std::vector<std::string> encode_path_vector_;
  std::vector<std::string> signature_vector_;
  std::vector<int64_t> size_list_;
  std::string uri_;
  std::string remote_tree_uuid_;
  int32_t local_tree_id_;
  SSL_CTX* ctx_;
  int64_t total_size_;
  ITaskMonitor* monitor_;
  int32_t task_id_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TAR_PUT_TASK_H_
