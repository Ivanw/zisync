// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_DISCOVER_H_
#define ZISYNC_KERNEL_DISCOVER_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class IDiscoverWorker {
 public:
  virtual ~IDiscoverWorker() { /* virtual destructor */ }

  virtual err_t Initialize(int32_t route_port) = 0;
  virtual err_t CleanUp() = 0;

#ifdef ZS_TEST
  virtual err_t SearchCacheClear() = 0;
#endif

  virtual err_t SetDiscoverPort(int32_t discover_port) = 0;

  static const char cmd_uri[];
  static const char pull_uri[];
  static IDiscoverWorker* GetInstance();

 protected:
  IDiscoverWorker()  {
  }

 private:
  IDiscoverWorker(IDiscoverWorker&);
  void operator=(IDiscoverWorker&);
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_DISCOVER_H_
