// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_REFRESH_WORKER_H_
#define ZISYNC_KERNEL_WORKER_REFRESH_WORKER_H_

#include <map>
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/worker/worker.h"

namespace zs {

class RefreshWorker : public Worker {
 public:
  explicit RefreshWorker();

 private:
  RefreshWorker(RefreshWorker&);
  void operator=(RefreshWorker&);

  virtual err_t Initialize();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_REFRESH_WORKER_H_
