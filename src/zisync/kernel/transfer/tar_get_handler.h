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

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_GET_HANDLER_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_GET_HANDLER_H_

#include <libtar.h>
#include <string>
#include <vector>

#include "zisync/kernel/transfer/transfer_server.h"

namespace zs {

class OsTcpSocket;

class TarGetHandlerFactory : public TaskHandlerFactory {
  friend class TransferServer;
 public:
  virtual ~TarGetHandlerFactory() {}

  virtual ITaskHandler* CreateTaskHandler();
  virtual const std::string& GetFormat() {
    return format_;
  }
  virtual const std::string& GetMethod() {
    return method_;
  }

 private:
  TarGetHandlerFactory() {
    method_ = "GET";
    format_ = "tar";
  }

  std::string method_;
  std::string format_;
  static TarGetHandlerFactory s_tar_get_handler_factory_;
};

class TarGetHandler : public ITaskHandler {
  friend class TarGetHandlerFactory;
 public:
  virtual ~TarGetHandler() {}

  virtual err_t OnHandleTask(
      IAbortable* abortable, const std::string& local_tree_uuid,
      const std::string& remote_tree_uuid, 
      std::istream& in, std::ostream& out, ITaskMonitor* monitor);

 private:
  TarGetHandler() {}
};

}  // namespace zs
#endif  // ZISYNC_KERNEL_TRANSFER_TAR_GET_HANDLER_H_
