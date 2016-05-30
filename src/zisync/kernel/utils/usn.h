// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_USN_H_
#define ZISYNC_KERNEL_UTILS_USN_H_

#include <cstdint>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

int64_t GetUsn();
err_t GetTreeMaxUsnFromContent(const char *tree_uuid, int64_t *tree_max_usn);
err_t GetMaxUsnFromContent();

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_UNS_H_
