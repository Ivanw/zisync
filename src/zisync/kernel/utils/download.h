// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_DOWNLOAD_H_
#define ZISYNC_KERNEL_UTILS_DOWNLOAD_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class IDownload {
 public:
  virtual ~IDownload() {}
  virtual bool Initialize() = 0;
  virtual err_t Startup(
      int32_t sync_id, const std::string &relative_path, 
      std::string *target_path, int32_t *task_id) = 0;
  virtual err_t Shutdown(int32_t task_id) = 0;
  virtual err_t QueryStatus(int32_t task_id, DownloadStatus *status) = 0;
  virtual void CleanUp() = 0;
  virtual bool ReduceDownloadCacheVolume(int64_t new_volume) = 0;
  virtual bool HasDownloadCache(
      const std::string &absolute_path, int64_t file_mtime, 
      int64_t file_length) = 0;
  virtual int64_t GetDownloadCacheAmount() = 0;
  virtual err_t CleanUpDownloadCache() = 0;

  static IDownload* GetInstance();
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_DOWNLOAD_H_
