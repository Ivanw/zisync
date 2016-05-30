// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_UPLOAD_H_
#define ZISYNC_KERNEL_UTILS_UPLOAD_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class IUpload {
 public:
  virtual ~IUpload() {}
  virtual err_t Startup(
      int32_t sync_id, const std::string &relative_path, 
      const std::string &real_path, int32_t *task_id) = 0;
  virtual err_t Shutdown(int32_t task_id) = 0;
  virtual err_t QueryStatus(int32_t task_id, UploadStatus *status) = 0;
  virtual void CleanUp() = 0;

  static IUpload* GetInstance();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_UPLOAD_H_
