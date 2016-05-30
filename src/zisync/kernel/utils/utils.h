// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_UTILS_H_
#define ZISYNC_KERNEL_UTILS_UTILS_H_

#include <stdint.h>
#include <cassert>
#include <string>
#include <algorithm>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"

namespace zs {

static inline bool IsValidPort(int32_t port) {
  return (port < 1024 || port > 65535) ? false : true;
}

bool IsSystemRootDir(const std::string &path);

static inline void FixTreeRoot(std::string *root) {
  if (IsSystemRootDir(*root)) {
    assert(root->length() > 0);
    root->erase(root->end() - 1);
  }
}

err_t GenDeviceRootForBackup(
    const char *device_name, std::string *device_backup_root);
bool HasMtimeChanged(int64_t mtime_in_fs, int64_t mtime_in_db);
bool HasAttrChanged(int32_t attr_in_fs, int32_t attr_in_db);
void GenConflictFilePath(
    std::string *conflict_file_path, const std::string &tree_root, 
    const std::string &file_path);

inline string GenDownloadTmpPath(
    const std::string &sync_uuid, const std::string &relative_path) {
  return Config::download_cache_dir() + "/" + sync_uuid + relative_path;
}

inline string GenFixedStringForDatabase(const std::string str) {
  string ret;
  std::for_each(
      str.begin(), str.end(), [&ret](const char &c) {
        ret.push_back(c);
        if (c == '\'') {
          ret.push_back(c);
        }
      });
  return ret;
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_UTILS_H_
