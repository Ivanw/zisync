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

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_PUT_HANDLER_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_PUT_HANDLER_H_

#include <libtar.h>
#include <string>
#include <vector>

#include "zisync/kernel/transfer/transfer_server.h"

namespace zs {

class OsTcpSocket;

class TarPutHandlerFactory : public TaskHandlerFactory {
  friend class TransferServer;
 public:
  virtual ~TarPutHandlerFactory() {}

  virtual ITaskHandler* CreateTaskHandler();
  virtual const std::string& GetFormat() {
    return format_;
  }
  virtual const std::string& GetMethod() {
    return method_;
  }

 private:
  TarPutHandlerFactory() {
    method_ = "PUT";
    format_ = "tar";
  }

  std::string method_;
  std::string format_;
  static TarPutHandlerFactory s_tar_put_handler_factory_;
};

/**
 * Send Put a bounch of file (including data) of the same tree to remote.
 *
 * The files are send as a tar stream carried by http body. that is the
 * socket stream has the following format:
 *
 * PUT tar HTTP/1.1\r\n
 * ZiSync-Tree-Uuid:00000000-0000-0000-0000-000000000000\r\n
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
class TarPutHandler : public ITaskHandler {
  friend class TarPutHandlerFactory;
 public:
  virtual ~TarPutHandler() {}

  virtual err_t OnHandleTask(
      IAbortable* abortable, const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid,
      std::istream& in, std::ostream& out,
      ITaskMonitor* monitor);

 private:
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TAR_PUT_HANDLER_H_
