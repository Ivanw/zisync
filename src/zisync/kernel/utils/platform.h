// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_PLATFORM_H_
#define ZISYNC_KERNEL_UTILS_PLATFORM_H_
#include "zisync/kernel/platform/platform.h"
#ifdef ZS_TEST
#include "zisync/kernel/utils/configure.h"
#endif
#include "zisync/kernel/zslog.h"

namespace zs {

static inline bool IsMobileDevice(Platform platform) {
  return platform == PLATFORM_IOS || platform == PLATFORM_ANDROID;
}

static inline bool IsMobileDevice() {
  Platform platform = GetPlatform();
#ifdef ZS_TEST
  if (Config::is_set_test_platform()) {
    platform = Config::test_platform();
  }
#endif
  return IsMobileDevice(platform);
}


}

#endif  // ZISYNC_KERNEL_UTILS_PLATFORM_H_
