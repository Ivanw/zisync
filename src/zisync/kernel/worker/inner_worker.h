// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_INNER_WORKER_H_
#define ZISYNC_KERNEL_WORKER_INNER_WORKER_H_

#include <map>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/worker/worker.h"

namespace zs {

class InnerWorker : public Worker {
 public:
  InnerWorker();

 private:
  InnerWorker(InnerWorker&);
  void operator=(InnerWorker&);
  virtual err_t Initialize();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_INNER_WORKER_H_
