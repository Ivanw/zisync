// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_WORKER_H_
#define ZISYNC_KERNEL_WORKER_WORKER_H_

#include <map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/proto/kernel.pb.h"

namespace zs {

class ZmqSocket;
class MessageHandler;

class Worker : public OsThread {
 public:
  Worker(const char *thread_name);
  virtual ~Worker();

  err_t Startup();
  virtual int Run();
  virtual err_t HandleRequest();
 
 protected:
  ZmqSocket *req, *exit;
  std::map<MsgCode, MessageHandler*> msg_handlers_;
  const string name;
 private:
  Worker(Worker&);
  void operator=(Worker&);
  virtual err_t Initialize() = 0;

};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_WORKER_H_
