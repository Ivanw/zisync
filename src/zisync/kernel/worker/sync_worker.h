// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_SYNC_WORKER_H_
#define ZISYNC_KERNEL_WORKER_SYNC_WORKER_H_

#include "zisync/kernel/worker/worker.h"

namespace zs {

class SyncWorker : public Worker {
 public:
  SyncWorker();
  virtual ~SyncWorker() {}
  
 private:
  SyncWorker(SyncWorker&);
  void operator=(SyncWorker&);
  
  virtual err_t Initialize();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_SYNC_WORKER_H_
