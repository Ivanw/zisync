// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_OUTER_WORKER_H_
#define ZISYNC_KERNEL_WORKER_OUTER_WORKER_H_

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/worker/worker.h"

namespace zs {

class OuterWorker : public Worker {
 public:
  OuterWorker();

 private:
  OuterWorker(OuterWorker&);
  void operator=(OuterWorker&);

  virtual err_t Initialize();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_OUTER_WORKER_H_
